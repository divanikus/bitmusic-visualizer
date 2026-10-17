#pragma once
#include "engine.h"

class PokeyTrack final : public MusicTrack {
public:
    PokeyTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture);
    ~PokeyTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(VoiceMask mask) override { muteMask_ = mask.low32() & 15; }
    void dry() override {} // No shared echo or artificial stereo.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int) const override { return {-1}; }
    TrackBoundary boundary() const override;
    qint64 frame() const override { return frame_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
    uint32_t muteMask_ = 0;
};
