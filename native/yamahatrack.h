#pragma once
#include "engine.h"

struct VgmHeader;
// Supported Yamaha VGM chips expose their independent hardware voices.
class YamahaTrack final : public MusicTrack {
public:
    YamahaTrack(const QString &path, QByteArray data, int song, bool playlist, bool capture,
                std::shared_ptr<const YamahaRoms> roms = {});
    ~YamahaTrack() override;
    const TrackInfo &info() const override { return info_; }
    bool seek(qint64 frame, const std::function<bool()> &cancelled = [] { return false; }) override;
    void render(short *out, int frames) override;
    void mute(VoiceMask mask) override { muteMask_ = mask; }
    void dry() override {} // No added effects to disable.
    bool hasTaps() const override;
    bool readTap(int channel, QVector<float> &left, QVector<float> &right) const override;
    QVector<float> notePitches(int) const override { return {-1}; }
    TrackBoundary boundary() const override;
    qint64 frame() const override { return frame_; }
private:
    void readMetadata(const QString &path, const QByteArray &data, const VgmHeader &header, bool playlist);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_ = 0;
    VoiceMask muteMask_ = 0;
};
