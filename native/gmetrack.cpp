#include "engine.h"
#include "vgm.h"
#include "gme-taps/taps.h"
#include "gme-taps/notes.h"
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
        info_.configuration = soundConfiguration(data, info_.voices);
        // Patched libgme exposes 8 voices for YM2612 and 10 for mono YM2413 + PSG.
        // NSF/NSFE stay mono; stereo-capable chips retain stereo during centered passages.
        info_.stereoOutput = data.startsWith("SNES-SPC700") || data.startsWith("GBS") ||
            (data.startsWith("Vgm ") && (count == 8 || (count != 10 && psgHasStereoRouting(data))));
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
