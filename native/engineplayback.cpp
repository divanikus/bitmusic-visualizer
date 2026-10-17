#include "engine.h"
#include "outputhistory.h"
#include <QAudioSink>
#include <QMediaDevices>
#include <QTimer>
#include <stdexcept>

// Created and destroyed in NativePlayer::run. Decoders and Qt audio objects
// remain confined to the audio worker; the callback only consumes PCM.
class NativePlayer::AudioWorker {
public:
    explicit AudioWorker(NativePlayer &player) : player_(player) {}

    void stopOutput() {
        player_.enabled_ = false;
        if (sink_) { sink_->reset(); sink_.reset(); }
        player_.ring_.reset();
        playing_ = false;
    }

    void process() {
        std::optional<Command> command;
        { std::lock_guard lock(player_.stateMutex_); command.swap(player_.pending_); }
        try {
            if (command && !execute(*command)) return;
            advancePlayback();
            updateMetadata();
        } catch (const std::exception &error) {
            reportFailure(error, command && command->load);
        }
    }

private:
    // Publication is generation-gated; cancellation also observes shutdown.
    bool isCurrent() const { return active_ == player_.generation_; }
    bool cancelled() const { return player_.closing_ || !isCurrent(); }

    // False ends this tick after unload or cancellation.
    bool execute(const Command &command) {
        active_ = command.generation;
        stopOutput();
        if (command.unload) {
            track_.reset();
            player_.playedFrame_ = 0;
            player_.endFrame_ = -1;
            player_.durationLimited_ = false;
            std::lock_guard lock(player_.stateMutex_);
            if (isCurrent()) { player_.state_ = {}; player_.state_.generation = active_; }
            return false;
        }
        if (command.load) {
            track_ = openMusicTrack(command.path, command.song);
            activeLimit_ = player_.unknownDurationLimit_.load();
        }
        if (!track_) throw std::runtime_error("Open a music file first.");
        if (!seekTrack(command)) return false;
        playing_ = command.playing && (player_.endFrame_ < 0 || player_.playedFrame_ < player_.endFrame_);
        if (playing_ && !startOutput()) return false;
        std::lock_guard lock(player_.stateMutex_);
        if (isCurrent()) {
            player_.state_.info = track_->info();
            player_.state_.instruments = track_->instrumentNames(player_.playedFrame_.load());
            player_.state_.pattern = track_->patternPosition(player_.playedFrame_.load());
            player_.state_.valid = true; player_.state_.busy = false; player_.state_.playing = playing_;
        }
        return true;
    }

    bool seekTrack(const Command &command) {
        updateEnd();
        int target = command.ms;
        const int endMs = playbackEndMs(player_.endFrame_.load());
        if (endMs >= 0) target = std::min(target, endMs);
        if (!command.load && command.playing && !player_.state().playing &&
            player_.endFrame_ >= 0 && player_.playedFrame_ >= player_.endFrame_ && target >= endMs)
            target = 0;
        if (!track_->seek(qint64(target) * SampleRate / 1000, [this] { return cancelled(); })) return false;
        player_.playedFrame_ = track_->frame();
        updateEnd();
        return true;
    }

    bool startOutput() {
        QAudioFormat format;
        format.setSampleRate(SampleRate);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        const auto device = QMediaDevices::defaultAudioOutput();
        if (device.isNull() || !device.isFormatSupported(format))
            throw std::runtime_error("Default output does not support stereo 44100 Hz / 16-bit audio.");
        sink_ = std::make_unique<QAudioSink>(device, format);
        fill();
        if (cancelled()) { stopOutput(); return false; }
        player_.enabled_ = true;
        sink_->start([player = &player_, callbackGeneration = active_](QSpan<int16_t> output) {
            std::fill(output.begin(), output.end(), 0);
            if (!player->enabled_.load(std::memory_order_acquire) || callbackGeneration != player->generation_) return;
            // No allocation, mutex, emulation or GUI calls on this thread.
            const auto copied = player->ring_.pop(output.data(), output.size());
            applyOutputGain(output.data(), copied, player->volume_.load(std::memory_order_relaxed),
                            player->outputMask_.load(std::memory_order_relaxed));
            player->playedFrame_.fetch_add(copied / 2);
            const auto captureToken = player->outputToken_.load(std::memory_order_relaxed);
            if (captureToken & 1)
                player->outputHistory_->push(output.data(), size_t(output.size()) / 2, callbackGeneration, captureToken,
                                             int(player->playedFrame_.load() * 1000 / SampleRate));
            if (copied < static_cast<size_t>(output.size()) &&
                (player->endFrame_ < 0 || player->playedFrame_ < player->endFrame_)) ++player->starvations_;
        });
        checkOutput("Cannot start audio output.");
        return true;
    }

    void advancePlayback() {
        if (!playing_ || !isCurrent()) return;
        checkOutput("Audio output device failed.");
        fill();
        if (player_.endFrame_ >= 0 && player_.playedFrame_ >= player_.endFrame_) {
            stopOutput();
            std::lock_guard lock(player_.stateMutex_);
            if (isCurrent()) {
                player_.state_.playing = false;
                player_.state_.finished = true;
                // Natural transitions happen off the GUI thread. A user's newer command wins.
                const bool repeat = player_.repeat_.load();
                if (!player_.pending_ && (repeat || (player_.autoSubsong_ && track_->info().song + 1 < track_->info().songs))) {
                    const auto next = ++player_.generation_;
                    player_.state_.generation = next;
                    player_.state_.busy = true;
                    player_.state_.finished = false;
                    const auto boundary = track_->boundary();
                    const int loopMs = boundary.loopFrame >= 0 ? int(boundary.loopFrame * 1000 / SampleRate) : track_->info().loopStartMs;
                    if (repeat) player_.pending_ = Command{next, {}, 0, player_.durationLimited_ ? 0 : loopMs, false, true};
                    else player_.pending_ = Command{next, track_->info().path, track_->info().song + 1, 0, true, true};
                }
            }
        }
    }

    void updateMetadata() {
        if (track_ && isCurrent()) {
            const auto names = track_->instrumentNames(player_.playedFrame_.load());
            const auto pattern = track_->patternPosition(player_.playedFrame_.load());
            std::lock_guard lock(player_.stateMutex_);
            if (isCurrent()) { player_.state_.instruments = names; player_.state_.pattern = pattern; }
        }
    }

    void checkOutput(const char *message) {
        if (sink_ && sink_->error() != QtAudio::NoError) {
            ++player_.outputErrors_;
            throw std::runtime_error(message);
        }
    }

    void reportFailure(const std::exception &error, bool loading) {
        stopOutput();
        std::lock_guard lock(player_.stateMutex_);
        if (isCurrent()) {
            player_.state_.busy = player_.state_.playing = false;
            player_.state_.error = QString::fromUtf8(error.what());
            if (loading) { track_.reset(); player_.state_.valid = false; }
        }
    }

    void updateEnd() {
        qint64 end = track_->info().durationMs < 0 ? -1 : qint64(track_->info().durationMs) * SampleRate / 1000;
        const auto boundary = track_->boundary();
        if (boundary.frame >= 0 && (end < 0 || boundary.frame < end)) end = boundary.frame;
        const bool limited = end < 0 && activeLimit_ > 0;
        if (limited) end = qint64(activeLimit_) * SampleRate / 1000;
        player_.endFrame_ = end;
        player_.durationLimited_ = limited;
    }

    void fill() {
        if (!track_) return;
        track_->mute(player_.muteMask_.load());
        // At most ~186 ms ahead: keeps mute response bounded while covering scheduling jitter.
        while (player_.ring_.size() < 8192 * 2 && !cancelled()) {
            auto count = qint64(BlockFrames);
            if (player_.endFrame_ >= 0) count = std::min(count, player_.endFrame_.load() - track_->frame());
            if (count <= 0) break;
            const auto start = track_->frame();
            track_->render(buffer_.data(), static_cast<int>(count));
            updateEnd(); // A decoder may discover a loop while producing this block.
            if (player_.endFrame_ >= 0) count = std::clamp<qint64>(player_.endFrame_.load() - start, 0, count);
            player_.ring_.push(buffer_.data(), static_cast<size_t>(count) * 2);
        }
    }

    NativePlayer &player_;
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<MusicTrack> track_;
    std::array<short, BlockFrames * 2> buffer_{};
    quint64 active_ = 0;
    bool playing_ = false;
    int activeLimit_ = 0;
};

void NativePlayer::run() {
    QObject context;
    AudioWorker audio(*this);
    // Scope work owns Qt objects (including a QThreadPool). Let Qt perform
    // thread cleanup before native TLS teardown; an adopted std::thread caused
    // a multi-second join delay on Windows even after scopeLoop had returned.
    std::unique_ptr<QThread> scopeThread(QThread::create([this] { scopeLoop(); }));
    scopeThread->start(QThread::NormalPriority); // Do not inherit the audio decoder's high priority.
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(3);
    QObject::connect(&timer, &QTimer::timeout, &context, [&] {
        if (closing_) { quit(); return; }
        audio.process();
    });
    timer.start();
    if (!closing_) exec();
    audio.stopOutput();
    { std::lock_guard lock(scopeWaitMutex_); closing_ = true; }
    scopeWake_.notify_all();
    scopeThread->wait();
}
