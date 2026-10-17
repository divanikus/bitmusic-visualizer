#include "engine.h"
#include "outputhistory.h"

void ScopeFrame::retainChannels(VoiceMask visible) {
    mask &= visible;
    for (int i = 0; i < channels.size(); ++i) if (!(mask & VoiceMask::bit(i))) {
        for (auto *rows : {&channels, &left, &right})
            if (i < rows->size()) (*rows)[i].clear();
    }
}

void NativePlayer::setScopesEnabled(bool enabled) {
    {
        std::lock_guard lock(scopeWaitMutex_);
        if (scopesEnabled_.exchange(enabled) == enabled) return;
        ++scopeRevision_;
    }
    // A reopened window must not paint history from before it was hidden.
    { std::lock_guard lock(scopeMutex_); scope_ = {}; }
    scopeWake_.notify_all();
}
void NativePlayer::setScopeFrameRate(int fps) {
    { std::lock_guard lock(scopeWaitMutex_); scopeFrameRate_.store(ScopePacing::normalize(fps)); }
    // Only reschedule capture. Changing FPS must not invalidate prepared voices.
    scopeWake_.notify_all();
}
void NativePlayer::setScopeMask(VoiceMask mask) {
    {
        std::lock_guard lock(scopeWaitMutex_);
        if (scopeMask_.exchange(mask) == mask) return;
        ++scopeRevision_;
    }
    // Keep current traces for channels that remain visible. New channels become
    // ready independently; a layout edit must not rewind every solo emulator.
    {
        std::lock_guard lock(scopeMutex_);
        scope_.retainChannels(mask);
    }
    scopeWake_.notify_all();
}
ScopeFrame NativePlayer::scopes() const { std::lock_guard lock(scopeMutex_); return scope_; }
void NativePlayer::setOutputScopesEnabled(bool enabled) {
    const auto token = outputToken_.load();
    if (outputCaptureEnabled(token) != enabled) outputToken_ = ((token + 2) & ~quint64(1)) | quint64(enabled);
}
OutputFrame NativePlayer::outputScopes() const {
    const auto token = outputToken_.load();
    if (!outputCaptureEnabled(token)) return {};
    auto result = outputHistory_->read();
    std::lock_guard lock(stateMutex_);
    if (token != outputToken_ || state_.busy) return {};
    if (outputFrameMatches(result, generation_, token)) return result;
    if (outputFrameMatches(pausedOutput_, generation_, token)) return pausedOutput_;
    return {};
}
