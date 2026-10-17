#include "engine.h"
#include "outputhistory.h"
#include <algorithm>
#include <cstring>
#include <limits>

void applyOutputGain(short *samples, size_t count, float volume, uint32_t outputMask) {
    for (size_t i = 0; i < count; ++i)
        samples[i] = (outputMask & (1u << (i & 1))) ? static_cast<short>(samples[i] * volume) : 0;
}

size_t PcmRing::size() const { return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire); }
bool PcmRing::push(const short *source, size_t count) {
    const auto write = write_.load(std::memory_order_relaxed);
    if (count > Capacity - (write - read_.load(std::memory_order_acquire))) return false;
    const auto offset = static_cast<size_t>(write % Capacity);
    const auto first = std::min(count, Capacity - offset);
    std::memcpy(samples_.data() + offset, source, first * sizeof(short));
    std::memcpy(samples_.data(), source + first, (count - first) * sizeof(short));
    write_.store(write + count, std::memory_order_release);
    return true;
}
size_t PcmRing::pop(short *destination, size_t count) {
    const auto read = read_.load(std::memory_order_relaxed);
    count = std::min(count, static_cast<size_t>(write_.load(std::memory_order_acquire) - read));
    const auto offset = static_cast<size_t>(read % Capacity);
    const auto first = std::min(count, Capacity - offset);
    std::memcpy(destination, samples_.data() + offset, first * sizeof(short));
    std::memcpy(destination + first, samples_.data(), (count - first) * sizeof(short));
    read_.store(read + count, std::memory_order_release);
    return count;
}
void PcmRing::reset() { read_.store(0); write_.store(0); }


NativePlayer::NativePlayer() : outputHistory_(std::make_unique<OutputHistory>()) { start(QThread::HighPriority); }
NativePlayer::~NativePlayer() { shutdown(); }
void NativePlayer::shutdown() {
    { std::lock_guard lock(scopeWaitMutex_); closing_.store(true); }
    scopeWake_.notify_all();
    enabled_.store(false);
    quit();
    wait();
}
void NativePlayer::submit(Command command) {
    std::lock_guard lock(stateMutex_);
    pausedOutput_ = {};
    // Pause at the current playhead freezes the last delivered output. A seek
    // elsewhere has no delivered audio yet and must not display stale samples.
    if (!command.load && !command.unload && !command.playing && state_.playing &&
        std::abs(command.ms - int(playedFrame_.load() * 1000 / SampleRate)) <= 30) {
        auto captured = outputHistory_->read();
        if (outputFrameMatches(captured, generation_, outputToken_) && outputCaptureEnabled(captured.token))
            pausedOutput_ = std::move(captured);
    }
    enabled_.store(false);
    command.generation = ++generation_;
    if (pausedOutput_.sequence) pausedOutput_.generation = command.generation;
    state_.generation = command.generation;
    state_.busy = true;
    state_.finished = false;
    state_.error.clear();
    state_.scopeError.clear();
    pending_ = std::move(command);
}
void NativePlayer::load(const QString &path, int song) {
    const auto current = state();
    if (!current.valid || current.info.path != path) setMuteMask(0);
    submit({0, path, song, 0, true, true});
}
void NativePlayer::seek(int ms, bool playing) { submit({0, {}, 0, std::clamp(ms, 0, 3600000), false, playing}); }
void NativePlayer::unload() { submit({0, {}, 0, 0, false, false, true}); }
void NativePlayer::setMuteMask(VoiceMask mask) {
    std::lock_guard lock(stateMutex_);
    // A manual mute action takes over the currently audible set.
    state_.soloChannel = -1;
    muteMask_.store(mask);
}
void NativePlayer::toggleSolo(int channel) {
    std::lock_guard lock(stateMutex_);
    if (!state_.valid || state_.busy || channel < 0 || channel >= state_.info.voices.size() || channel >= VoiceMask::Capacity) return;
    if (state_.soloChannel == channel) {
        state_.soloChannel = -1;
        muteMask_.store(beforeSoloMask_);
    } else {
        if (state_.soloChannel < 0) beforeSoloMask_ = muteMask_.load();
        state_.soloChannel = channel;
        muteMask_.store(~(VoiceMask::bit(channel)));
    }
}
int NativePlayer::playbackEndMs(qint64 frame) {
    if (frame < 0) return -1;
    return int(std::min<qint64>((frame * 1000 + SampleRate - 1) / SampleRate, std::numeric_limits<int>::max()));
}
PlayerState NativePlayer::state() const {
    std::lock_guard lock(stateMutex_);
    auto result = state_;
    result.positionMs = static_cast<int>(playedFrame_.load() * 1000 / SampleRate);
    // A millisecond endpoint may lie between samples. Flooring twice otherwise
    // leaves the UI one millisecond short, so Play never recognizes the end.
    const auto end=endFrame_.load();
    result.playbackEndMs = playbackEndMs(end);
    result.durationLimited=durationLimited_.load();
    if (state_.valid && !state_.busy && end >= 0 && playedFrame_ >= end)
        result.positionMs = result.playbackEndMs;
    result.muteMask = muteMask_.load();
    result.outputMask = outputMask_.load();
    result.repeat = repeat_.load();
    result.starvations = starvations_.load();
    result.outputErrors = outputErrors_.load();
    result.scopeBlocks = scopeBlocks_.load();
    result.sharedScopes = sharedScopes_.load();
    result.scopesSuspended = scopesSuspended_.load();
    return result;
}
