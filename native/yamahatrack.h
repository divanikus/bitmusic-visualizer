#pragma once
#include "engine.h"

// YM2203 hardware voices; remaining Yamaha families currently expose chip mixes.
class YamahaTrack final : public MusicTrack {
public:
    YamahaTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture,
                std::shared_ptr<const YamahaRoms> roms = {});
    ~YamahaTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(uint32_t mask) override { muteMask_ = mask; }
    void dry() override {} // Chip mix includes its complete synthesis, with no added effects.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int) const override { return {-1}; }
    qint64 frame() const override { return frame_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
    uint32_t muteMask_ = 0;
};
