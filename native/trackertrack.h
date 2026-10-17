#pragma once
#include "engine.h"

bool isTrackerModule(const QByteArray &data);
class TrackerTrack final : public MusicTrack {
public:
    TrackerTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture);
    ~TrackerTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 target, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(VoiceMask mask) override;
    void dry() override {} // Source taps already use the same per-voice mixer as audio.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int) const override { return {-1}; } // Sample pitch is not necessarily a musical note.
    QStringList instrumentNames(qint64 frame) const override;
    PatternPosition patternPosition(qint64 frame) const override;
    TrackBoundary boundary() const override;
    qint64 frame() const override { return frame_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
};
