#pragma once
#include "engine.h"

class SidTrack final : public MusicTrack {
public:
    SidTrack(const QString &path, int song = 0, bool readPlaylist = true, bool capture = false);
    ~SidTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 target, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(uint32_t mask) override;
    void dry() override {} // Captured voices are already before the shared filter.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int) const override { return {-1}; }
    qint64 frame() const override { return frame_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
};
