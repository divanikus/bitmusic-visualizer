#include "engine.h"
#include "vgm.h"
#include "gmevgm.h"
#include "gme-taps/taps.h"
#include "gme-taps/notes.h"
#include <gme.h>
#include <QtEndian>
#include <QFileInfo>
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
}

void GmeTrack::readMetadata(bool readPlaylist) {
    const auto &path = info_.path;
    const int song = info_.song;
    for (int index = readPlaylist ? 0 : song; index < (readPlaylist ? info_.songs : song + 1); ++index) {
        gme_info_t *raw = nullptr;
        check(gme_track_info(emu_, &raw, index));
        const std::unique_ptr<gme_info_t, decltype(&gme_free_info)> owned(raw, &gme_free_info);
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
    }
}

GmeTrack::GmeTrack(const QString &path, int song, bool readPlaylist, bool capture, QByteArray data) {
    if (data.isEmpty()) data = readMusicFile(path);
    const bool vgm = data.startsWith("Vgm ");
    VgmHeader header;
    quint32 psgClock = 0, opllClock = 0, fmClock = 0;
    if (vgm) {
        header = inspectVgm(data); requireSupportedVgm(header);
        if (header.pokeyClock) throw std::runtime_error("POKEY VGM requires the POKEY decoder.");
        if (!header.yamaha.isEmpty()) throw std::runtime_error("This VGM requires the Yamaha decoder.");
        boundary_ = gmeVgmBoundary(data, header.dataOffset);
        psgClock = qFromLittleEndian<quint32>(data.constData() + 12);
        opllClock = qFromLittleEndian<quint32>(data.constData() + 16);
        fmClock = qFromLittleEndian<quint32>(data.constData() + 44);
    }
    const auto clockFlags = (psgClock | opllClock | fmClock) & 0xc0000000u;
    const bool psgNotes = vgm && !clockFlags;
    const bool threeToneNotes = data.startsWith("NESM\x1a") || data.startsWith("NSFE") ||
        data.startsWith("GBS") || data.startsWith("ZXAYEMUL");
    if (capture) {
        if (threeToneNotes || psgNotes) notes_ = bm_notes_create();
        if (data.startsWith("SNES-SPC700")) taps_ = bm_taps_create(3);
        else if (vgm) {
            // Dual chips keep the existing path until their channel identities are separated.
            if (header.version >= 0x110 && !clockFlags) {
                if (fmClock) taps_ = bm_taps_create(1);
                else if (opllClock) taps_ = bm_taps_create(2);
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
        readMetadata(readPlaylist);
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
        info_.configuration = vgm ? vgmSoundConfiguration(data, info_.voices, header) : soundConfiguration(data, info_.voices);
        // Patched libgme exposes 8 voices for YM2612 and 10 for mono YM2413 + PSG.
        // NSF/NSFE stay mono; stereo-capable chips retain stereo during centered passages.
        info_.stereoOutput = data.startsWith("SNES-SPC700") || data.startsWith("GBS") ||
            (vgm && (count == 8 || (count != 10 && gmeVgmStereoRouting(data, header.dataOffset))));
        check(gme_start_track(emu_, song));
    } catch (...) { release(); throw; }
}
TrackBoundary GmeTrack::boundary() const {
    if(boundary_.frame>=0)return boundary_;
    return gme_track_ended(emu_)?TrackBoundary{frame_,-1}:TrackBoundary{};
}
GmeTrack::~GmeTrack() { release(); }
void GmeTrack::release() {
    if (emu_) gme_delete(emu_);
    emu_ = nullptr;
    bm_taps_delete(taps_); taps_ = nullptr;
    bm_notes_delete(notes_); notes_ = nullptr;
}
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
