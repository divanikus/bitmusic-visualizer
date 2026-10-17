#pragma once
#include "engine.h"
#include "midisequence.h"

// A single immutable snapshot is shared by audio, scopes and cold seeks.
struct Mt32Resources {
    MidiSequence sequence;
    QVector<SongInfo> songs;
    int song = 0;
    QByteArray control, pcm;
    QString model;
};
QString playerMt32Directory();
QString selectedMidiEngine(); // auto, gm, mt32
void selectMidiEngine(const QString &engine);
std::unique_ptr<MusicTrack> openMidiTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture);

class Mt32Track final : public MusicTrack {
public:
    Mt32Track(const QString &path, QByteArray data, int song, bool playlist, bool capture,
              std::shared_ptr<const Mt32Resources> resources = {});
    ~Mt32Track() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 target, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(VoiceMask mask) override { muteMask_ = mask; }
    void dry() override {} // Part taps precede shared reverb/analogue stages.
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
