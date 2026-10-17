#pragma once
#include "engine.h"
#include "midisequence.h"
#include "soundfontbank.h"

struct MidiResources {
    std::shared_ptr<const NoteRoll> noteRoll;
    MidiSequence sequence;
    QVector<SongInfo> songs;
    int song = 0;
    QVector<SoundFontBank> soundFonts;
    QString soundFontName;
};

class MidiTrack final : public MusicTrack {
public:
    MidiTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture,
              std::shared_ptr<const MidiResources> resources = {});
    ~MidiTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 target, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(VoiceMask mask) override { muteMask_ = mask; }
    void dry() override {} // Taps already exclude chorus and reverb.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int channel) const override;
    QStringList instrumentNames(qint64 frame) const override;
    qint64 frame() const override { return frame_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
    VoiceMask muteMask_ = 0;
};
