#include "engine.h"
#include "vgm.h"
#include "trackertrack.h"
#include "tia-replay/replay.h"
#include "outputhistory.h"
#include "gme-taps/taps.h"
#include "gme-taps/notes.h"
#include <QtEndian>
#include <QFile>
#include <QFileInfo>
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

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
TrackBoundary gmeVgmBoundary(const QByteArray &data) {
    auto word=[&](qsizetype at) { return qFromLittleEndian<quint32>(data.constData()+at); };
    if(data.size()<0x40)return {};
    quint64 pos=word(8)>=0x150 && word(0x34)?0x34ULL+word(0x34):0x40;
    const quint64 loop=word(0x1c)?0x1cULL+word(0x1c):0;
    qint64 ticks=0,loopFrame=-1;
    while(pos<quint64(data.size())) {
        if(pos==loop)loopFrame=ticks;
        const auto op=quint8(data[qsizetype(pos++)]);
        const auto remaining=quint64(data.size())-pos;
        if(op==0x66)return ticks>0 && (!loop || (loopFrame>=0 && loopFrame<ticks))?TrackBoundary{ticks,loopFrame}:TrackBoundary{};
        if(op==0x61) {
            if(remaining<2)return {};
            ticks+=qFromLittleEndian<quint16>(data.constData()+pos);pos+=2;
        } else if(op==0x62)ticks+=735;
        else if(op==0x63)ticks+=882;
        else if(op>=0x70 && op<=0x7f)ticks+=(op&15)+1;
        else if(op>=0x80 && op<=0x8f)ticks+=op&15;
        else if(op==0x4f || op==0x50 || op==0x3f || op==0x30) {if(!remaining)return {}; ++pos;}
        else if((op>=0x51 && op<=0x53) || (op>=0xa1 && op<=0xa3)) {if(remaining<2)return {}; pos+=2;}
        else if(op==0xe0) {if(remaining<4)return {}; pos+=4;}
        else if(op==0x67) {
            if(remaining<6 || quint8(data[qsizetype(pos)])!=0x66)return {};
            const quint64 size=word(qsizetype(pos+2))&0x7fffffffu;
            if(size>remaining-6)return {};
            pos+=6+size;
        } else return {}; // Do not guess timing for an unsupported/legacy command.
    }
    return {};
}
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
}
QByteArray readMusicFile(const QString &path) {
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
          data.startsWith("Vgm ") || data.startsWith("SNES-SPC700") || data.startsWith("GBS") || data.startsWith("ZXAYEMUL") || data.startsWith("MThd") || Ttt::recognizes(data) || isTrackerModule(data)))
        throw std::runtime_error("Supported formats: NSF, NSFE, VGM, VGZ, SPC, GBS, AY (ZXAYEMUL), SID (PSID/RSID), MIDI (SMF 0/1), TTT (TIATracker), tracker modules (MOD, XM, S3M, IT, MED and other libxmp formats).");
    return data;
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

GmeTrack::GmeTrack(const QString &path, int song, bool readPlaylist, bool capture, QByteArray data) {
    if (data.isEmpty()) data = readMusicFile(path);
    if (data.startsWith("Vgm ")) {
        const auto header = inspectVgm(data); requireSupportedVgm(header);
        if (header.pokeyClock) throw std::runtime_error("POKEY VGM requires the POKEY decoder.");
        if (!header.yamaha.isEmpty()) throw std::runtime_error("This VGM requires the Yamaha decoder.");
    }
    if(data.startsWith("Vgm "))boundary_=gmeVgmBoundary(data);
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
                psgNoteChannel_ = count-1; info_.tonalMask = VoiceMask::bit(psgNoteChannel_);
            }
        }
        for (int i = 0; i < count; ++i) info_.voices << QString::fromUtf8(gme_voice_name(emu_, i));
        if (data.startsWith("ZXAYEMUL") && count == 4) info_.voices = {"AY A", "AY B", "AY C", "Beeper"};
        info_.configuration = soundConfiguration(data, info_.voices);
        // Patched libgme exposes 8 voices for YM2612 and 10 for mono YM2413 + PSG.
        // NSF/NSFE stay mono; stereo-capable chips retain stereo during centered passages.
        info_.stereoOutput = data.startsWith("SNES-SPC700") || data.startsWith("GBS") ||
            (data.startsWith("Vgm ") && (count == 8 || (count != 10 && psgHasStereoRouting(data))));
        check(gme_start_track(emu_, song));
    } catch (...) { if (emu_) gme_delete(emu_); emu_ = nullptr; bm_taps_delete(taps_); taps_ = nullptr; bm_notes_delete(notes_); notes_ = nullptr; throw; }
}
TrackBoundary GmeTrack::boundary() const {
    if(boundary_.frame>=0)return boundary_;
    return gme_track_ended(emu_)?TrackBoundary{frame_,-1}:TrackBoundary{};
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
    if (channel < 0 || channel >= VoiceMask::Capacity || !(info_.tonalMask & (VoiceMask::bit(channel)))) return {-1};
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
void GmeTrack::mute(VoiceMask mask) { muteMask_ = mask; gme_mute_voices(emu_, static_cast<int>(mask.low32())); }
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
        scope_.mask &= mask;
        for (int i = 0; i < scope_.channels.size(); ++i)
            if (!(scope_.mask & (VoiceMask::bit(i)))) {
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
