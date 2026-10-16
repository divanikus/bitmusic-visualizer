#pragma once
#include "engine.h"
class TttTrack final : public MusicTrack {
public:
    TttTrack(const QString &path,QByteArray data,int song,bool playlist,bool capture);
    ~TttTrack() override;
    const TrackInfo &info() const override {return info_;}
    bool seek(qint64 frame,const std::function<bool()> &cancelled=[] {return false;}) override;
    void render(short *out,int frames) override;
    void mute(VoiceMask mask) override;
    void dry() override {}
    bool hasTaps() const override;
    bool readTap(int channel,QVector<float> &left,QVector<float> &right) const override;
    QVector<float> notePitches(int) const override {return {-1};}
    QStringList instrumentNames(qint64 frame) const override;
    qint64 frame() const override {return frame_;}
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackInfo info_;
    qint64 frame_=0;
};
