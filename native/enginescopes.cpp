#include "engine.h"
#include <QSemaphore>
#include <QThreadPool>
#include <chrono>
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
            {
                std::unique_lock lock(player_.scopeWaitMutex_);
                if (!player_.scopesEnabled_ || !player_.scopeMask_) player_.scopesSuspended_ = true;
                player_.scopeWake_.wait(lock, [this] { return player_.closing_ || (player_.scopesEnabled_ && player_.scopeMask_); });
                if (player_.closing_) break;
                player_.scopesSuspended_ = false;
            }
            const auto revision = player_.scopeRevision_.load();
            const auto fps = player_.scopeFrameRate_.load();
            const auto mask = player_.scopeMask_.load();
            const auto current = player_.state();
            const auto tick = std::chrono::steady_clock::now();
            bool preparing = false;
            if (current.valid && !current.busy && current.error.isEmpty()) {
                try {
                    const auto cancelled = [&] { return player_.closing_ || !player_.scopesEnabled_ || revision != player_.scopeRevision_ || current.generation != player_.generation_; };
                    const qint64 target = qint64(current.positionMs) * SampleRate / 1000;
                    if (path_ != current.info.path || song_ != current.info.song || scopeRoms_ != current.info.sidRoms ||
                        scopeYamahaRoms_ != current.info.yamahaRoms || scopeMidiResources_ != current.info.midiResources || scopeMt32Resources_ != current.info.mt32Resources) {
                        voices_.clear(); voices_.resize(current.info.voices.size());
                        path_ = current.info.path; song_ = current.info.song;
                        scopeRoms_ = current.info.sidRoms;
                        scopeYamahaRoms_ = current.info.yamahaRoms;
                        scopeMidiResources_ = current.info.midiResources;
                        scopeMt32Resources_ = current.info.mt32Resources;
                        shared_ = openMusicTrack(path_, song_, false, true, current.info.sidRoms, current.info.yamahaRoms, current.info.midiResources, current.info.mt32Resources);
                        if (!shared_->hasTaps()) shared_.reset();
                        player_.sharedScopes_ = bool(shared_);
                    }
                    if (shared_) {
                        if ((current.info.midiResources || current.info.mt32Resources) && (shared_->frame() > target || target-shared_->frame() > 2*SampleRate)) {
                            if (!shared_->seek(target, cancelled)) continue;
                        } else if (shared_->frame() > target) shared_->seek(0, cancelled);
                        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
                        std::array<short, BlockFrames * 2> scratch;
                        while (shared_->frame() < target && !cancelled()) {
                            shared_->render(scratch.data(), int(std::min<qint64>(BlockFrames, target - shared_->frame())));
                            ++player_.scopeBlocks_;
                            if (std::chrono::steady_clock::now() >= deadline) break;
                        }
                        if (cancelled()) continue;
                        if (shared_->frame() < target) continue;
                        ScopeFrame frame;
                        frame.generation = current.generation; frame.positionMs = current.positionMs;
                        const int count = int(voices_.size());
                        frame.channels.resize(count); frame.left.resize(count); frame.right.resize(count);
                        frame.noteHz.resize(count);
                        frame.instruments = shared_->instrumentNames(target);
                        for (int i = 0; i < count; ++i) if (mask & (VoiceMask::bit(i))) {
                            if (shared_->readTap(i, frame.left[i], frame.right[i])) {
                                frame.channels[i].resize(ScopeFrames);
                                for (int j = 0; j < ScopeFrames; ++j) frame.channels[i][j] = (frame.left[i][j] + frame.right[i][j]) * .5f;
                                frame.mask |= VoiceMask::bit(i);
                                frame.noteHz[i] = shared_->notePitches(i);
                            }
                        }
                        { std::lock_guard lock(player_.scopeMutex_); if (!cancelled()) player_.scope_ = std::move(frame); }
                        std::unique_lock lock(player_.scopeWaitMutex_);
                        player_.scopeWake_.wait_until(lock, tick + ScopePacing::interval(fps), [this, revision, fps] {
                            return player_.closing_ || !player_.scopesEnabled_ || revision != player_.scopeRevision_ || fps != player_.scopeFrameRate_;
                        });
                        continue;
                    }
                    VoiceMask ready = 0;
                    auto renderToTarget = [&](Voice &voice) {
                        std::array<short, ScopeFrames * 2> samples{};
                        if (voice.history.isEmpty()) {
                            voice.history.fill(0, ScopeFrames); voice.left.fill(0, ScopeFrames); voice.right.fill(0, ScopeFrames);
                        }
                        while (voice.track->frame() < target && !cancelled()) {
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
                    };
                    // Advance working channels first, without seeking or clearing their history.
                    for (size_t i = 0; i < voices_.size() && !cancelled(); ++i) {
                        auto &voice = voices_[i]; const auto bit = VoiceMask::bit(i);
                        if (!(mask & bit)) { voice = {}; continue; }
                        if (!voice.track || voice.history.isEmpty()) continue;
                        const auto lag = target - voice.track->frame();
                        if (lag < 0 || lag > SampleRate / 2) { voice.history.clear(); continue; }
                        renderToTarget(voice); ready |= bit;
                    }
                    // Prepare a bounded batch in parallel, preserving the dedicated
                    // master/audio threads. Finish this batch before admitting more
                    // voices_, so some panels return promptly even after a long seek.
                    SharedVoiceMask prepared{ready};
                    std::exception_ptr preparationError;
                    std::mutex errorMutex;
                    int pending = 0;
                    for (size_t checked = 0; checked < voices_.size() && !cancelled() && pending < preparationPool_.maxThreadCount(); ++checked) {
                        const auto i = checked; const auto bit = VoiceMask::bit(i);
                        if (!(mask & bit) || (ready & bit)) continue;
                        ++pending;
                        preparationPool_.start([&, i, bit] {
                            try {
                                auto &voice = voices_[i];
                                if (!voice.track) {
                                    voice.track = openMusicTrack(path_, song_, false, true, current.info.sidRoms, current.info.yamahaRoms, current.info.midiResources, current.info.mt32Resources);
                                    voice.track->mute(~bit); voice.track->dry();
                                }
                                if (voice.track->frame() > target) voice.track->seek(0, cancelled);
                                const auto historyStart = std::max<qint64>(0, target - ScopeFrames);
                                const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
                                while (voice.track->frame() < historyStart && !cancelled()) {
                                    voice.track->seek(std::min(historyStart, voice.track->frame() + SampleRate), cancelled);
                                    if (std::chrono::steady_clock::now() >= deadline) break;
                                }
                                if (voice.track->frame() >= historyStart && !cancelled()) {
                                    renderToTarget(voice); prepared.fetch_or(bit);
                                }
                            } catch (...) {
                                std::lock_guard lock(errorMutex);
                                if (!preparationError) preparationError = std::current_exception();
                            }
                            completed_.release();
                        });
                    }
                    completed_.acquire(pending);
                    if (preparationError) std::rethrow_exception(preparationError);
                    ready = prepared.load();
                    if (!cancelled()) {
                        QVector<QVector<float>> history(static_cast<int>(voices_.size()));
                        QVector<QVector<float>> left(history.size()), right(history.size());
                        QVector<QVector<float>> notes(history.size(), QVector<float>{-1});
                        for (size_t i = 0; i < voices_.size(); ++i)
                            if (ready & (VoiceMask::bit(i))) {
                                history[static_cast<int>(i)] = voices_[i].history;
                                notes[int(i)] = voices_[i].track->notePitches(int(i));
                                left[static_cast<int>(i)] = voices_[i].left; right[static_cast<int>(i)] = voices_[i].right;
                            }
                        std::lock_guard lock(player_.scopeMutex_);
                        if (!cancelled()) player_.scope_ = {current.generation, current.positionMs, history, ready, left, right, notes};
                        const auto available = VoiceMask::first(int(voices_.size()));
                        preparing = (ready & mask) != (mask & available);
                    }
                } catch (const std::exception &error) {
                    { std::lock_guard lock(player_.stateMutex_); if (current.generation == player_.generation_ && revision == player_.scopeRevision_) player_.state_.scopeError = QString::fromUtf8(error.what()); }
                    voices_.clear(); shared_.reset(); player_.sharedScopes_ = false; path_.clear();
                }
            }
            // Sleeping every 8 ms of preparation made a full grid barely keep up
            // with playback, so distant seeks could remain Preparing indefinitely.
            if (preparing) continue;
            std::unique_lock lock(player_.scopeWaitMutex_);
            player_.scopeWake_.wait_until(lock, tick + ScopePacing::interval(fps), [this, revision, fps] {
                return player_.closing_ || !player_.scopesEnabled_ || revision != player_.scopeRevision_ || fps != player_.scopeFrameRate_;
            });
        }
        preparationPool_.waitForDone();
    }

private:
    struct Voice { std::unique_ptr<MusicTrack> track; QVector<float> history, left, right; };
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
