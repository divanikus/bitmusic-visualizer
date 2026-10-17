#include "engine.h"
#include <QSemaphore>
#include <QThreadPool>
#include <chrono>
#include <exception>
#include <vector>

// Own all capture decoders and preparation tasks on the existing scope thread.
// Each batch is joined before its request and local captures leave scope.
class NativePlayer::ScopeWorker {
public:
    explicit ScopeWorker(NativePlayer &player) : player_(player) {
        preparationPool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 2, 1, 4));
        preparationPool_.setThreadPriority(QThread::LowPriority);
    }

    void run() {
        while (!player_.closing_) {
            if (!waitUntilEnabled()) break;
            const Request request{player_.scopeRevision_.load(), player_.scopeFrameRate_.load(),
                                  player_.scopeMask_.load(), player_.state(), Clock::now()};
            bool preparing = false;
            if (request.current.valid && !request.current.busy && request.current.error.isEmpty()) {
                try {
                    synchronizeTrack(request.current.info);
                    if (shared_) {
                        if (!captureShared(request)) continue;
                        waitForNextFrame(request);
                        continue;
                    }
                    preparing = captureVoices(request);
                } catch (const std::exception &error) {
                    reportFailure(request, error);
                }
            }
            // Sleeping every 8 ms of preparation made a full grid barely keep up
            // with playback, so distant seeks could remain Preparing indefinitely.
            if (preparing) continue;
            waitForNextFrame(request);
        }
        preparationPool_.waitForDone();
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Voice { std::unique_ptr<MusicTrack> track; QVector<float> history, left, right; };
    struct Request {
        quint64 revision;
        int fps;
        VoiceMask mask;
        PlayerState current;
        Clock::time_point tick;
        qint64 target() const { return qint64(current.positionMs) * SampleRate / 1000; }
    };

    bool waitUntilEnabled() {
        std::unique_lock lock(player_.scopeWaitMutex_);
        if (!player_.scopesEnabled_ || !player_.scopeMask_) player_.scopesSuspended_ = true;
        player_.scopeWake_.wait(lock, [this] { return player_.closing_ || (player_.scopesEnabled_ && player_.scopeMask_); });
        if (player_.closing_) return false;
        player_.scopesSuspended_ = false;
        return true;
    }

    void waitForNextFrame(const Request &request) {
        const auto revision = request.revision;
        const auto fps = request.fps;
        std::unique_lock lock(player_.scopeWaitMutex_);
        player_.scopeWake_.wait_until(lock, request.tick + ScopePacing::interval(fps), [this, revision, fps] {
            return player_.closing_ || !player_.scopesEnabled_ || revision != player_.scopeRevision_ || fps != player_.scopeFrameRate_;
        });
    }

    bool cancelled(const Request &request) const {
        return player_.closing_ || !player_.scopesEnabled_ || request.revision != player_.scopeRevision_ ||
               request.current.generation != player_.generation_;
    }

    static std::unique_ptr<MusicTrack> openCapture(const TrackInfo &info) {
        return openMusicTrack(info.path, info.song, false, true, info.sidRoms, info.yamahaRoms, info.midiResources, info.mt32Resources);
    }

    void synchronizeTrack(const TrackInfo &info) {
        if (path_ != info.path || song_ != info.song || scopeRoms_ != info.sidRoms ||
            scopeYamahaRoms_ != info.yamahaRoms || scopeMidiResources_ != info.midiResources || scopeMt32Resources_ != info.mt32Resources) {
            voices_.clear(); voices_.resize(info.voices.size());
            path_ = info.path; song_ = info.song;
            scopeRoms_ = info.sidRoms;
            scopeYamahaRoms_ = info.yamahaRoms;
            scopeMidiResources_ = info.midiResources;
            scopeMt32Resources_ = info.mt32Resources;
            shared_ = openCapture(info);
            if (!shared_->hasTaps()) shared_.reset();
            player_.sharedScopes_ = bool(shared_);
        }
    }

    ScopeFrame makeFrame(const Request &request) const {
        ScopeFrame frame;
        frame.generation = request.current.generation;
        frame.positionMs = request.current.positionMs;
        const int count = int(voices_.size());
        frame.channels.resize(count); frame.left.resize(count); frame.right.resize(count);
        frame.noteHz.resize(count);
        return frame;
    }

    void publish(const Request &request, ScopeFrame frame) {
        std::lock_guard lock(player_.scopeMutex_);
        if (!cancelled(request)) player_.scope_ = std::move(frame);
    }

    bool captureShared(const Request &request) {
        const auto target = request.target();
        const auto cancelled = [this, &request] { return this->cancelled(request); };
        if ((request.current.info.midiResources || request.current.info.mt32Resources) &&
            (shared_->frame() > target || target - shared_->frame() > 2 * SampleRate)) {
            if (!shared_->seek(target, cancelled)) return false;
        } else if (shared_->frame() > target) shared_->seek(0, cancelled);
        const auto deadline = Clock::now() + std::chrono::milliseconds(8);
        std::array<short, BlockFrames * 2> scratch;
        while (shared_->frame() < target && !cancelled()) {
            shared_->render(scratch.data(), int(std::min<qint64>(BlockFrames, target - shared_->frame())));
            ++player_.scopeBlocks_;
            if (Clock::now() >= deadline) break;
        }
        if (cancelled()) return false;
        if (shared_->frame() < target) return false;
        auto frame = makeFrame(request);
        frame.instruments = shared_->instrumentNames(target);
        for (int i = 0; i < frame.channels.size(); ++i) if (request.mask & VoiceMask::bit(i)) {
            if (shared_->readTap(i, frame.left[i], frame.right[i])) {
                frame.channels[i].resize(ScopeFrames);
                for (int j = 0; j < ScopeFrames; ++j) frame.channels[i][j] = (frame.left[i][j] + frame.right[i][j]) * .5f;
                frame.mask |= VoiceMask::bit(i);
                frame.noteHz[i] = shared_->notePitches(i);
            }
        }
        publish(request, std::move(frame));
        return true;
    }

    void renderToTarget(Voice &voice, const Request &request) {
        const auto target = request.target();
        std::array<short, ScopeFrames * 2> samples{};
        if (voice.history.isEmpty()) {
            voice.history.fill(0, ScopeFrames); voice.left.fill(0, ScopeFrames); voice.right.fill(0, ScopeFrames);
        }
        while (voice.track->frame() < target && !cancelled(request)) {
            const int count = static_cast<int>(std::min<qint64>(ScopeFrames, target - voice.track->frame()));
            voice.track->render(samples.data(), count);
            ++player_.scopeBlocks_;
            auto &row = voice.history;
            std::move(row.begin() + count, row.end(), row.begin());
            std::move(voice.left.begin() + count, voice.left.end(), voice.left.begin());
            std::move(voice.right.begin() + count, voice.right.end(), voice.right.begin());
            for (int j = 0; j < count; ++j) {
                row[ScopeFrames - count + j] = (float(samples[j * 2]) + samples[j * 2 + 1]) / 2;
                voice.left[ScopeFrames - count + j] = samples[j * 2];
                voice.right[ScopeFrames - count + j] = samples[j * 2 + 1];
            }
        }
    }

    VoiceMask advanceReadyVoices(const Request &request) {
        const auto target = request.target();
        VoiceMask ready = 0;
        // Advance working channels first, without seeking or clearing their history.
        for (size_t i = 0; i < voices_.size() && !cancelled(request); ++i) {
            auto &voice = voices_[i]; const auto bit = VoiceMask::bit(i);
            if (!(request.mask & bit)) { voice = {}; continue; }
            if (!voice.track || voice.history.isEmpty()) continue;
            const auto lag = target - voice.track->frame();
            if (lag < 0 || lag > SampleRate / 2) { voice.history.clear(); continue; }
            renderToTarget(voice, request); ready |= bit;
        }
        return ready;
    }

    bool prepareVoice(size_t i, const Request &request) {
        const auto target = request.target();
        const auto cancelled = [this, &request] { return this->cancelled(request); };
        auto &voice = voices_[i];
        if (!voice.track) {
            voice.track = openCapture(request.current.info);
            voice.track->mute(~VoiceMask::bit(i)); voice.track->dry();
        }
        if (voice.track->frame() > target) voice.track->seek(0, cancelled);
        const auto historyStart = std::max<qint64>(0, target - ScopeFrames);
        const auto deadline = Clock::now() + std::chrono::milliseconds(8);
        while (voice.track->frame() < historyStart && !cancelled()) {
            voice.track->seek(std::min(historyStart, voice.track->frame() + SampleRate), cancelled);
            if (Clock::now() >= deadline) break;
        }
        if (voice.track->frame() >= historyStart && !cancelled()) {
            renderToTarget(voice, request);
            return true;
        }
        return false;
    }

    VoiceMask prepareBatch(const Request &request, VoiceMask ready) {
        // Finish this bounded batch before admitting more voices. Some panels
        // return promptly after long seeks; audio keeps its dedicated thread.
        SharedVoiceMask prepared{ready};
        std::exception_ptr preparationError;
        std::mutex errorMutex;
        int pending = 0;
        for (size_t i = 0; i < voices_.size() && !cancelled(request) && pending < preparationPool_.maxThreadCount(); ++i) {
            const auto bit = VoiceMask::bit(i);
            if (!(request.mask & bit) || (ready & bit)) continue;
            ++pending;
            preparationPool_.start([&, i, bit] {
                try {
                    if (prepareVoice(i, request)) prepared.fetch_or(bit);
                } catch (...) {
                    std::lock_guard lock(errorMutex);
                    if (!preparationError) preparationError = std::current_exception();
                }
                completed_.release();
            });
        }
        completed_.acquire(pending);
        if (preparationError) std::rethrow_exception(preparationError);
        return prepared.load();
    }

    bool captureVoices(const Request &request) {
        auto ready = advanceReadyVoices(request);
        ready = prepareBatch(request, ready);
        if (!cancelled(request)) {
            auto frame = makeFrame(request);
            frame.mask = ready;
            frame.noteHz.fill(QVector<float>{-1});
            for (size_t i = 0; i < voices_.size(); ++i) if (ready & VoiceMask::bit(i)) {
                frame.channels[int(i)] = voices_[i].history;
                frame.noteHz[int(i)] = voices_[i].track->notePitches(int(i));
                frame.left[int(i)] = voices_[i].left; frame.right[int(i)] = voices_[i].right;
            }
            publish(request, std::move(frame));
            const auto available = VoiceMask::first(int(voices_.size()));
            return (ready & request.mask) != (request.mask & available);
        }
        return false;
    }

    void reportFailure(const Request &request, const std::exception &error) {
        {
            std::lock_guard lock(player_.stateMutex_);
            if (request.current.generation == player_.generation_ && request.revision == player_.scopeRevision_)
                player_.state_.scopeError = QString::fromUtf8(error.what());
        }
        voices_.clear(); shared_.reset(); player_.sharedScopes_ = false; path_.clear();
    }

    NativePlayer &player_;
    std::vector<Voice> voices_;
    std::unique_ptr<MusicTrack> shared_;
    std::shared_ptr<const SidRoms> scopeRoms_;
    std::shared_ptr<const YamahaRoms> scopeYamahaRoms_;
    std::shared_ptr<const MidiResources> scopeMidiResources_;
    std::shared_ptr<const Mt32Resources> scopeMt32Resources_;
    QString path_;
    int song_ = -1;
    QThreadPool preparationPool_;
    QSemaphore completed_;
};

void NativePlayer::scopeLoop() {
    ScopeWorker(*this).run();
}
