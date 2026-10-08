#include "engine.h"
#include "outputhistory.h"
#include "gme-taps/taps.h"
#include "gme-taps/notes.h"
#include <QtEndian>
#include <QAudioSink>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSemaphore>
#include <QThreadPool>
#include <QTimer>
#include <zlib.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
struct NoteScope {
    BmNotes* previous;
    explicit NoteScope(BmNotes* notes) : previous(bm_notes_enter(notes)) {}
    ~NoteScope() { bm_notes_enter(previous); }
};
struct TapScope {
    BmTaps* previous;
    explicit TapScope(BmTaps* taps) : previous(bm_taps_enter(taps)) {}
    ~TapScope() { bm_taps_enter(previous); }
};
void check(gme_err_t error) { if (error) throw std::runtime_error(error); }
bool psgHasStereoRouting(const QByteArray &data) {
    // Used only for libgme's PSG-only VGM path. FM paths have fixed capabilities.
    if (data.size() < 0x40) return false;
    auto word = [&](qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); };
    if ((word(12) & 0xc0000000u) == 0xc0000000u) return true; // T6W28 routing.
    quint64 pos = word(8) >= 0x150 && word(0x34) ? quint64(0x34) + word(0x34) : 0x40;
    const quint64 end = quint64(data.size());
    // Walk command boundaries, not raw bytes: sample data/metadata can contain 0x4f.
    while (pos < end) {
        const auto command = static_cast<unsigned char>(data[qsizetype(pos)]);
        if (command == 0x66) break;
        quint64 length = 1;
        if (command == 0x67) {
            if (end - pos < 7) break;
            length = 7ULL + (word(qsizetype(pos + 3)) & 0x7fffffffu);
        } else if (command == 0x68) length = 12;
        else if (command == 0x50 || command == 0x64) length = 2; // 0x64 is a byte delay in this backend.
        else if (command == 0x61) length = 3;
        else if (command >= 0x90 && command <= 0x95) {
            constexpr int lengths[] = {5, 5, 6, 11, 2, 5}; length = lengths[command - 0x90];
        } else if (command >= 0x30 && command < 0x50) length = 2;
        else if ((command >= 0x51 && command <= 0x5f) || (command >= 0xa0 && command <= 0xbf)) length = 3;
        else if (command >= 0xc0 && command <= 0xdf) length = 4;
        else if (command >= 0xe0) length = 5;
        if (length > end - pos) break;
        if (command == 0x4f || command == 0x3f) {
            const auto pan = static_cast<unsigned char>(data[qsizetype(pos + 1)]);
            if ((pan & 15) != (pan >> 4)) return true;
        }
        pos += length;
    }
    return false;
}
QByteArray readMusic(const QString &path) {
    constexpr int limit = 64 * 1024 * 1024;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(file.errorString().toStdString());
    auto data = file.read(limit + 1);
    if (data.size() > limit) throw std::runtime_error("Music file exceeds 64 MB.");
    if (data.startsWith(QByteArray::fromHex("1f8b"))) {
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(data.data());
        stream.avail_in = static_cast<uInt>(data.size());
        if (inflateInit2(&stream, 15 + 16) != Z_OK) throw std::runtime_error("Cannot initialize gzip decoder.");
        QByteArray decoded;
        std::array<char, 65536> chunk{};
        int result = Z_OK;
        while (result == Z_OK && decoded.size() <= limit) {
            stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
            stream.avail_out = chunk.size();
            result = inflate(&stream, Z_NO_FLUSH);
            decoded.append(chunk.data(), static_cast<int>(chunk.size() - stream.avail_out));
        }
        inflateEnd(&stream);
        if (result != Z_STREAM_END || decoded.size() > limit)
            throw std::runtime_error("Invalid, truncated or oversized gzip music file.");
        data = std::move(decoded);
    }
    if (!(data.startsWith("NESM\x1a") || data.startsWith("NSFE") ||
          data.startsWith("Vgm ") || data.startsWith("SNES-SPC700") || data.startsWith("GBS") || data.startsWith("ZXAYEMUL")))
        throw std::runtime_error("Supported formats: NSF, NSFE, VGM, VGZ, SPC, GBS, AY (ZXAYEMUL).");
    return data;
}
}

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

GmeTrack::GmeTrack(const QString &path, int song, bool readPlaylist, bool capture) {
    const auto data = readMusic(path);
    const bool psgNotes = data.startsWith("Vgm ") && data.size() >= 0x40 &&
        !((qFromLittleEndian<quint32>(data.constData()+12) | qFromLittleEndian<quint32>(data.constData()+16) |
           qFromLittleEndian<quint32>(data.constData()+44)) & 0xc0000000u);
    const bool threeToneNotes = data.startsWith("NESM\x1a") || data.startsWith("NSFE") ||
        data.startsWith("GBS") || data.startsWith("ZXAYEMUL");
    if (capture) {
        if (threeToneNotes || psgNotes) notes_ = bm_notes_create();
        if (data.startsWith("SNES-SPC700")) taps_ = bm_taps_create(3);
        else if (data.startsWith("Vgm ") && data.size() >= 0x40) {
            auto word = [&](int at) { return qFromLittleEndian<quint32>(data.constData() + at); };
            // Dual chips keep the existing path until their channel identities are separated.
            if (word(8) >= 0x110 && !((word(12) | word(16) | word(44)) & 0xc0000000u)) {
                if (word(44)) taps_ = bm_taps_create(1);
                else if (word(16)) taps_ = bm_taps_create(2);
            }
        }
    }
    TapScope captureScope(taps_); NoteScope noteScope(notes_);
    try {
        check(gme_open_data(data.constData(), static_cast<long>(data.size()), &emu_, SampleRate));
        gme_ignore_silence(emu_, 1);
        gme_set_autoload_playback_limit(emu_, 0);
        info_.tonalMask = threeToneNotes ? 7u : 0u;
        info_.songs = gme_track_count(emu_);
        if (song < 0 || song >= info_.songs) throw std::runtime_error("Invalid subsong number.");
        info_.path = path;
        info_.song = song;
        for (int index = readPlaylist ? 0 : song; index < (readPlaylist ? info_.songs : song + 1); ++index) {
            gme_info_t *raw = nullptr;
            check(gme_track_info(emu_, &raw, index));
            SongInfo entry;
            entry.title = QString::fromUtf8(raw->song).trimmed();
            if (entry.title.isEmpty() && info_.songs == 1) entry.title = QString::fromUtf8(raw->game).trimmed();
            if (entry.title.isEmpty()) entry.title = info_.songs == 1 ? QFileInfo(path).completeBaseName()
                : QString::fromUtf8("Track %1").arg(index + 1, 2, 10, QLatin1Char('0'));
            // Do not use play_length: libgme invents a default duration for unknown files.
            if (raw->length > 0) entry.durationMs = raw->length;
            else if (raw->loop_length > 0) {
                const qint64 end = qint64(std::max(0, raw->intro_length)) + raw->loop_length;
                if (end <= std::numeric_limits<int>::max()) entry.durationMs = static_cast<int>(end);
            }
            if (raw->loop_length > 0 && raw->intro_length >= 0 && raw->intro_length < entry.durationMs)
                entry.loopStartMs = raw->intro_length;
            if (index == song) {
                info_.title = entry.title;
                info_.system = QString::fromUtf8(raw->system);
                info_.durationMs = entry.durationMs;
                info_.loopStartMs = entry.loopStartMs;
            }
            if (readPlaylist) info_.playlist.push_back(entry);
            gme_free_info(raw);
        }
        const int count = gme_voice_count(emu_);
        if (count < 1 || count > 32) throw std::runtime_error("Unsupported voice count.");
        if (psgNotes) {
            if (count == 4) info_.tonalMask = 7;
            else if (count == 8 || count == 10) {
                psgNoteChannel_ = count-1; info_.tonalMask = 1u << psgNoteChannel_;
            }
        }
        for (int i = 0; i < count; ++i) info_.voices << QString::fromUtf8(gme_voice_name(emu_, i));
        if (data.startsWith("ZXAYEMUL") && count == 4) info_.voices = {"AY A", "AY B", "AY C", "Beeper"};
        // Patched libgme exposes 8 voices for YM2612 and 10 for mono YM2413 + PSG.
        // NSF/NSFE stay mono; stereo-capable chips retain stereo during centered passages.
        info_.stereoOutput = data.startsWith("SNES-SPC700") || data.startsWith("GBS") ||
            (data.startsWith("Vgm ") && (count == 8 || (count != 10 && psgHasStereoRouting(data))));
        check(gme_start_track(emu_, song));
    } catch (...) { if (emu_) gme_delete(emu_); emu_ = nullptr; bm_taps_delete(taps_); taps_ = nullptr; bm_notes_delete(notes_); notes_ = nullptr; throw; }
}
GmeTrack::~GmeTrack() { if (emu_) gme_delete(emu_); bm_taps_delete(taps_); bm_notes_delete(notes_); }
bool GmeTrack::seek(qint64 target, const std::function<bool()> &cancelled) {
    TapScope captureScope(taps_); NoteScope noteScope(notes_);
    target = std::clamp<qint64>(target, 0, 3600LL * SampleRate);
    if (target < frame_) {
        bm_taps_reset(taps_); bm_notes_reset(notes_);
        check(gme_start_track(emu_, info_.song)); frame_ = 0;
        // Chip reset can clear the hardware mute flags (Nuked YM2612), even
        // though libgme remembers its public mask. Restore before any samples.
        mute(muteMask_);
        if (dry_) dry();
    }
    while (frame_ < target) {
        if (cancelled()) return false;
        if (taps_) {
            std::array<short, BlockFrames * 2> scratch;
            render(scratch.data(), int(std::min<qint64>(BlockFrames, target - frame_)));
        } else {
            frame_ = std::min(target, frame_ + SampleRate);
            check(gme_seek_samples(emu_, static_cast<int>(frame_ * 2)));
        }
    }
    return !cancelled();
}
void GmeTrack::render(short *out, int frames) { TapScope captureScope(taps_); NoteScope noteScope(notes_); check(gme_play(emu_, frames * 2, out)); frame_ += frames; }
bool GmeTrack::readTap(int channel, QVector<float>& left, QVector<float>& right) const {
    left.resize(ScopeFrames); right.resize(ScopeFrames);
    return bm_taps_read(taps_, channel, frame_, ScopeFrames, left.data(), right.data());
}
float GmeTrack::noteHz(int channel) const {
    return notePitches(channel).value(0, 0);
}
QVector<float> GmeTrack::notePitches(int channel) const {
    if (channel < 0 || channel >= 32 || !(info_.tonalMask & (1u << channel))) return {-1};
    QVector<float> pitches;
    const bool grouped = channel == psgNoteChannel_;
    const int first = grouped ? 0 : channel, end = grouped ? 3 : channel+1;
    for (int voice = first; voice < end; ++voice) {
        float hz = -1;
        if (!bm_notes_read(notes_, voice, std::max<qint64>(0, frame_-1), &hz)) return {-1};
        if (hz > 0) pitches.push_back(hz);
    }
    return pitches;
}
void GmeTrack::mute(uint32_t mask) { muteMask_ = mask; gme_mute_voices(emu_, static_cast<int>(mask)); }
void GmeTrack::dry() { dry_ = true; gme_disable_echo(emu_, 1); }

NativePlayer::NativePlayer() : outputHistory_(std::make_unique<OutputHistory>()) { start(QThread::HighPriority); }
NativePlayer::~NativePlayer() { shutdown(); }
void NativePlayer::shutdown() {
    { std::lock_guard lock(scopeWaitMutex_); closing_.store(true); }
    scopeWake_.notify_all();
    enabled_.store(false);
    quit();
    wait();
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
void NativePlayer::setScopeMask(uint32_t mask) {
    {
        std::lock_guard lock(scopeWaitMutex_);
        if (scopeMask_.exchange(mask) == mask) return;
        ++scopeRevision_;
    }
    // Keep current traces for channels that remain visible. New channels become
    // ready independently; a layout edit must not rewind every solo emulator.
    {
        std::lock_guard lock(scopeMutex_);
        scope_.mask &= mask;
        for (int i = 0; i < scope_.channels.size(); ++i)
            if (!(scope_.mask & (uint32_t(1) << i))) {
                scope_.channels[i].clear();
                if (i < scope_.left.size()) scope_.left[i].clear();
                if (i < scope_.right.size()) scope_.right[i].clear();
            }
    }
    scopeWake_.notify_all();
}
void NativePlayer::submit(Command command) {
    std::lock_guard lock(stateMutex_);
    pausedOutput_ = {};
    // Pause at the current playhead freezes the last delivered output. A seek
    // elsewhere has no delivered audio yet and must not display stale samples.
    if (!command.load && !command.playing && state_.playing &&
        std::abs(command.ms - int(playedFrame_.load() * 1000 / SampleRate)) <= 30) {
        auto captured = outputHistory_->read();
        if (captured.generation == generation_ && captured.token == outputToken_ && (captured.token & 1))
            pausedOutput_ = std::move(captured);
    }
    enabled_.store(false);
    command.generation = ++generation_;
    if (pausedOutput_.sequence) pausedOutput_.generation = command.generation;
    state_.generation = command.generation;
    state_.busy = true;
    state_.error.clear();
    state_.scopeError.clear();
    pending_ = std::move(command);
}
void NativePlayer::load(const QString &path, int song) {
    const auto current = state();
    if (!current.valid || current.info.path != path) muteMask_ = 0;
    submit({0, path, song, 0, true, true});
}
void NativePlayer::seek(int ms, bool playing) { submit({0, {}, 0, std::clamp(ms, 0, 3600000), false, playing}); }
PlayerState NativePlayer::state() const {
    std::lock_guard lock(stateMutex_);
    auto result = state_;
    result.positionMs = static_cast<int>(playedFrame_.load() * 1000 / SampleRate);
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
ScopeFrame NativePlayer::scopes() const { std::lock_guard lock(scopeMutex_); return scope_; }
void NativePlayer::setOutputScopesEnabled(bool enabled) {
    const auto token = outputToken_.load();
    if (bool(token & 1) != enabled) outputToken_ = ((token + 2) & ~quint64(1)) | quint64(enabled);
}
OutputFrame NativePlayer::outputScopes() const {
    const auto token = outputToken_.load();
    if (!(token & 1)) return {};
    auto result = outputHistory_->read();
    std::lock_guard lock(stateMutex_);
    if (token != outputToken_ || state_.busy) return {};
    if (result.generation == generation_ && result.token == token) return result;
    if (pausedOutput_.generation == generation_ && pausedOutput_.token == token) return pausedOutput_;
    return {};
}

void NativePlayer::run() {
    QObject context;
    std::unique_ptr<QAudioSink> sink;
    std::unique_ptr<GmeTrack> track;
    std::array<short, BlockFrames * 2> buffer{};
    quint64 active = 0;
    bool playing = false;
    // Scope work owns Qt objects (including a QThreadPool). Let Qt perform
    // thread cleanup before native TLS teardown; an adopted std::thread caused
    // a multi-second join delay on Windows even after scopeLoop had returned.
    std::unique_ptr<QThread> scopeThread(QThread::create([this] { scopeLoop(); }));
    scopeThread->start(QThread::NormalPriority); // Do not inherit the audio decoder's high priority.
    auto stopOutput = [&] {
        enabled_ = false;
        if (sink) { sink->reset(); sink.reset(); }
        ring_.reset();
        playing = false;
    };
    auto fill = [&] {
        if (!track) return;
        track->mute(muteMask_.load());
        // At most ~186 ms ahead: keeps mute response bounded while covering scheduling jitter.
        while (ring_.size() < 8192 * 2 && active == generation_ && !closing_) {
            auto count = qint64(BlockFrames);
            if (endFrame_ >= 0) count = std::min(count, endFrame_.load() - track->frame());
            if (count <= 0) break;
            track->render(buffer.data(), static_cast<int>(count));
            ring_.push(buffer.data(), static_cast<size_t>(count) * 2);
        }
    };
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(3);
    QObject::connect(&timer, &QTimer::timeout, &context, [&] {
        if (closing_) { quit(); return; }
        std::optional<Command> command;
        { std::lock_guard lock(stateMutex_); command.swap(pending_); }
        try {
            if (command) {
                active = command->generation;
                stopOutput();
                if (command->load) track = std::make_unique<GmeTrack>(command->path, command->song);
                if (!track) throw std::runtime_error("Open a music file first.");
                int target = command->ms;
                if (track->info().durationMs >= 0) target = std::min(target, track->info().durationMs);
                if (!track->seek(qint64(target) * SampleRate / 1000, [&] { return closing_ || active != generation_; })) return;
                playedFrame_ = track->frame();
                endFrame_ = track->info().durationMs < 0 ? -1 : qint64(track->info().durationMs) * SampleRate / 1000;
                playing = command->playing && (endFrame_ < 0 || playedFrame_ < endFrame_);
                if (playing) {
                    QAudioFormat format;
                    format.setSampleRate(SampleRate);
                    format.setChannelCount(2);
                    format.setSampleFormat(QAudioFormat::Int16);
                    const auto device = QMediaDevices::defaultAudioOutput();
                    if (device.isNull() || !device.isFormatSupported(format))
                        throw std::runtime_error("Default output does not support stereo 44100 Hz / 16-bit audio.");
                    sink = std::make_unique<QAudioSink>(device, format);
                    fill();
                    if (active != generation_ || closing_) { stopOutput(); return; }
                    enabled_ = true;
                    sink->start([this, callbackGeneration = active](QSpan<int16_t> output) {
                        std::fill(output.begin(), output.end(), 0);
                        if (!enabled_.load(std::memory_order_acquire) || callbackGeneration != generation_) return;
                        // No allocation, mutex, emulation or GUI calls on this thread.
                        const auto copied = ring_.pop(output.data(), output.size());
                        applyOutputGain(output.data(), copied, volume_.load(std::memory_order_relaxed),
                                        outputMask_.load(std::memory_order_relaxed));
                        playedFrame_.fetch_add(copied / 2);
                        const auto captureToken = outputToken_.load(std::memory_order_relaxed);
                        if (captureToken & 1)
                            outputHistory_->push(output.data(), size_t(output.size()) / 2, callbackGeneration, captureToken,
                                                 int(playedFrame_.load() * 1000 / SampleRate));
                        if (copied < static_cast<size_t>(output.size()) &&
                            (endFrame_ < 0 || playedFrame_ < endFrame_)) ++starvations_;
                    });
                    if (sink->error() != QtAudio::NoError) { ++outputErrors_; throw std::runtime_error("Cannot start audio output."); }
                }
                std::lock_guard lock(stateMutex_);
                if (active == generation_) {
                    state_.info = track->info();
                    state_.valid = true; state_.busy = false; state_.playing = playing;
                }
            }
            if (playing && active == generation_) {
                if (sink && sink->error() != QtAudio::NoError) { ++outputErrors_; throw std::runtime_error("Audio output device failed."); }
                fill();
                if (endFrame_ >= 0 && playedFrame_ >= endFrame_) {
                    stopOutput();
                    std::lock_guard lock(stateMutex_);
                    if (active == generation_) {
                        state_.playing = false;
                        // Natural transitions happen off the GUI thread. A user's newer command wins.
                        const bool repeat = repeat_.load();
                        if (!pending_ && (repeat || track->info().song + 1 < track->info().songs)) {
                            const auto next = ++generation_;
                            state_.generation = next;
                            state_.busy = true;
                            if (repeat) pending_ = Command{next, {}, 0, track->info().loopStartMs, false, true};
                            else pending_ = Command{next, track->info().path, track->info().song + 1, 0, true, true};
                        }
                    }
                }
            }
        } catch (const std::exception &error) {
            stopOutput();
            std::lock_guard lock(stateMutex_);
            if (active == generation_) {
                state_.busy = state_.playing = false;
                state_.error = QString::fromUtf8(error.what());
                if (command && command->load) { track.reset(); state_.valid = false; }
            }
        }
    });
    timer.start();
    if (!closing_) exec();
    stopOutput();
    { std::lock_guard lock(scopeWaitMutex_); closing_ = true; }
    scopeWake_.notify_all();
    scopeThread->wait();
}

void NativePlayer::scopeLoop() {
    struct Voice { std::unique_ptr<GmeTrack> track; QVector<float> history, left, right; };
    std::vector<Voice> voices;
    std::unique_ptr<GmeTrack> shared;
    QString path;
    int song = -1;
    QThreadPool preparationPool;
    QSemaphore completed;
    preparationPool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 2, 1, 4));
    preparationPool.setThreadPriority(QThread::LowPriority);
    while (!closing_) {
        {
            std::unique_lock lock(scopeWaitMutex_);
            if (!scopesEnabled_ || !scopeMask_) scopesSuspended_ = true;
            scopeWake_.wait(lock, [this] { return closing_ || (scopesEnabled_ && scopeMask_); });
            if (closing_) break;
            scopesSuspended_ = false;
        }
        const auto revision = scopeRevision_.load();
        const auto fps = scopeFrameRate_.load();
        const auto mask = scopeMask_.load();
        const auto current = state();
        const auto tick = std::chrono::steady_clock::now();
        bool preparing = false;
        if (current.valid && !current.busy && current.error.isEmpty()) {
            try {
                const auto cancelled = [&] { return closing_ || !scopesEnabled_ || revision != scopeRevision_ || current.generation != generation_; };
                const qint64 target = qint64(current.positionMs) * SampleRate / 1000;
                if (path != current.info.path || song != current.info.song) {
                    voices.clear(); voices.resize(current.info.voices.size());
                    path = current.info.path; song = current.info.song;
                    shared = std::make_unique<GmeTrack>(path, song, false, true);
                    if (!shared->hasTaps()) shared.reset();
                    sharedScopes_ = bool(shared);
                }
                if (shared) {
                    if (shared->frame() > target) shared->seek(0, cancelled);
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
                    std::array<short, BlockFrames * 2> scratch;
                    while (shared->frame() < target && !cancelled()) {
                        shared->render(scratch.data(), int(std::min<qint64>(BlockFrames, target - shared->frame())));
                        ++scopeBlocks_;
                        if (std::chrono::steady_clock::now() >= deadline) break;
                    }
                    if (cancelled()) continue;
                    if (shared->frame() < target) continue;
                    ScopeFrame frame;
                    frame.generation = current.generation; frame.positionMs = current.positionMs;
                    const int count = int(voices.size());
                    frame.channels.resize(count); frame.left.resize(count); frame.right.resize(count);
                    frame.noteHz.resize(count);
                    for (int i = 0; i < count; ++i) if (mask & (uint32_t(1) << i)) {
                        if (shared->readTap(i, frame.left[i], frame.right[i])) {
                            frame.channels[i].resize(ScopeFrames);
                            for (int j = 0; j < ScopeFrames; ++j) frame.channels[i][j] = (frame.left[i][j] + frame.right[i][j]) * .5f;
                            frame.mask |= uint32_t(1) << i;
                            frame.noteHz[i] = shared->notePitches(i);
                        }
                    }
                    { std::lock_guard lock(scopeMutex_); if (!cancelled()) scope_ = std::move(frame); }
                    std::unique_lock lock(scopeWaitMutex_);
                    scopeWake_.wait_until(lock, tick + ScopePacing::interval(fps), [this, revision, fps] {
                        return closing_ || !scopesEnabled_ || revision != scopeRevision_ || fps != scopeFrameRate_;
                    });
                    continue;
                }
                uint32_t ready = 0;
                auto renderToTarget = [&](Voice &voice) {
                    std::array<short, ScopeFrames * 2> samples{};
                    if (voice.history.isEmpty()) {
                        voice.history.fill(0, ScopeFrames); voice.left.fill(0, ScopeFrames); voice.right.fill(0, ScopeFrames);
                    }
                    while (voice.track->frame() < target && !cancelled()) {
                        const int count = static_cast<int>(std::min<qint64>(ScopeFrames, target - voice.track->frame()));
                        voice.track->render(samples.data(), count);
                        ++scopeBlocks_;
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
                for (size_t i = 0; i < voices.size() && !cancelled(); ++i) {
                    auto &voice = voices[i]; const auto bit = uint32_t(1) << i;
                    if (!(mask & bit)) { voice = {}; continue; }
                    if (!voice.track || voice.history.isEmpty()) continue;
                    const auto lag = target - voice.track->frame();
                    if (lag < 0 || lag > SampleRate / 2) { voice.history.clear(); continue; }
                    renderToTarget(voice); ready |= bit;
                }
                // Prepare a bounded batch in parallel, preserving the dedicated
                // master/audio threads. Finish this batch before admitting more
                // voices, so some panels return promptly even after a long seek.
                std::atomic<uint32_t> prepared{ready};
                std::exception_ptr preparationError;
                std::mutex errorMutex;
                int pending = 0;
                for (size_t checked = 0; checked < voices.size() && !cancelled() && pending < preparationPool.maxThreadCount(); ++checked) {
                    const auto i = checked; const auto bit = uint32_t(1) << i;
                    if (!(mask & bit) || (ready & bit)) continue;
                    ++pending;
                    preparationPool.start([&, i, bit] {
                        try {
                            auto &voice = voices[i];
                            if (!voice.track) {
                                voice.track = std::make_unique<GmeTrack>(path, song, false, true);
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
                        completed.release();
                    });
                }
                completed.acquire(pending);
                if (preparationError) std::rethrow_exception(preparationError);
                ready = prepared.load();
                if (!cancelled()) {
                    QVector<QVector<float>> history(static_cast<int>(voices.size()));
                    QVector<QVector<float>> left(history.size()), right(history.size());
                    QVector<QVector<float>> notes(history.size(), QVector<float>{-1});
                    for (size_t i = 0; i < voices.size(); ++i)
                        if (ready & (uint32_t(1) << i)) {
                            history[static_cast<int>(i)] = voices[i].history;
                            notes[int(i)] = voices[i].track->notePitches(int(i));
                            left[static_cast<int>(i)] = voices[i].left; right[static_cast<int>(i)] = voices[i].right;
                        }
                    std::lock_guard lock(scopeMutex_);
                    if (!cancelled()) scope_ = {current.generation, current.positionMs, history, ready, left, right, notes};
                    const uint32_t available = voices.size() == 32 ? ~0u : (uint32_t(1) << voices.size()) - 1;
                    preparing = (ready & mask) != (mask & available);
                }
            } catch (const std::exception &error) {
                { std::lock_guard lock(stateMutex_); if (current.generation == generation_ && revision == scopeRevision_) state_.scopeError = QString::fromUtf8(error.what()); }
                voices.clear(); shared.reset(); sharedScopes_ = false; path.clear();
            }
        }
        // Sleeping every 8 ms of preparation made a full grid barely keep up
        // with playback, so distant seeks could remain Preparing indefinitely.
        if (preparing) continue;
        std::unique_lock lock(scopeWaitMutex_);
        scopeWake_.wait_until(lock, tick + ScopePacing::interval(fps), [this, revision, fps] {
            return closing_ || !scopesEnabled_ || revision != scopeRevision_ || fps != scopeFrameRate_;
        });
    }
    preparationPool.waitForDone();
}
