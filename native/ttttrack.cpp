#include "ttttrack.h"
#include "tia-replay/replay.h"
#include <TIASnd.h>
#include <QCryptographicHash>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct TttTrack::Impl {
    Ttt::Song song;
    Emulation::TIASound chip{31400};
    VoiceMask muted;
    bool capture;
    int tickFrames=882,phase=0;
    short raw[2]{};
    std::array<double,2> previous{},filtered{};
    std::array<std::array<float,ScopeFrames>,2> history{};
    explicit Impl(bool cap):capture(cap) {chip.channels(2,true);}
    void reset() {chip.reset();phase=0;raw[0]=raw[1]=0;previous={};filtered={};history={};}
    int tickAt(qint64 frame) const {
        qint64 tick=frame/tickFrames;
        if(tick>=song.ticks.size()) {
            if(song.loop<0) return -1;
            tick=song.loop+(tick-song.loop)%(song.ticks.size()-song.loop);
        }
        return int(tick);
    }
};
TttTrack::TttTrack(const QString &path,QByteArray data,int song,bool playlist,bool capture)
    :impl_(std::make_unique<Impl>(capture)) {
    if(song!=0) throw std::runtime_error("TIATracker projects contain one song.");
    auto &d=*impl_; d.song=Ttt::compile(data);
    if(d.song.ticks.isEmpty()) throw std::runtime_error("TIATracker project has no playable rows.");
    d.tickFrames=SampleRate/d.song.rate;
    info_.path=path;info_.song=0;info_.songs=1;info_.system="Atari 2600";
    info_.title=d.song.title.isEmpty()?QFileInfo(path).completeBaseName():d.song.title;
    info_.voices={"TIA 1","TIA 2"};info_.stereoOutput=false;
    info_.durationMs=int(qint64(d.song.ticks.size())*1000/d.song.rate);
    info_.loopStartMs=d.song.loop<0?0:int(qint64(d.song.loop)*1000/d.song.rate);
    if(playlist) info_.playlist.push_back({info_.title,info_.durationMs,info_.loopStartMs});
    auto &c=info_.configuration;c.family="atari2600";c.name="TIA";c.sources={"tia:0","tia:1"};
    c.id=QString::fromLatin1(QCryptographicHash::hash("atari2600|tia",QCryptographicHash::Sha256).toHex());
}
TttTrack::~TttTrack()=default;
void TttTrack::render(short *out,int frames) {
    auto &d=*impl_;
    // Integrate fixed-rate chip samples onto the host clock. Calling the old
    // core at 44.1 kHz directly makes its resampling depend on render chunk sizes.
    constexpr int clock=31400;
    const double dc=std::exp(-2.0*3.14159265358979323846*10.0/SampleRate);
    for(int i=0;i<frames;++i,++frame_) {
        const int tick=d.tickAt(frame_);
        if(frame_%d.tickFrames==0) {
            for(int c=0;c<2;++c) {
                const auto r=tick<0?std::array<int,3>{}:d.song.ticks[tick].registers[c];
                d.chip.set(Emulation::AUDC0+c,r[0]);
                d.chip.set(Emulation::AUDV0+c,r[2]);
                d.chip.set(Emulation::AUDF0+c,r[1]);
            }
        }
        double sum[2]{};int remain=clock;
        while(remain) {
            if(!d.phase) d.chip.process(d.raw,1);
            const int take=std::min(remain,SampleRate-d.phase);
            for(int c=0;c<2;++c) sum[c]+=double(d.raw[c])*take;
            remain-=take;d.phase=(d.phase+take)%SampleRate;
        }
        int mix=0;
        for(int c=0;c<2;++c) {
            const double input=sum[c]/clock;
            d.filtered[c]=input-d.previous[c]+dc*d.filtered[c];d.previous[c]=input;
            const int sample=tick<0?0:int(std::lround(d.filtered[c]));
            if(d.capture) d.history[c][frame_%ScopeFrames]=float(sample);
            if(!d.muted.test(c)) mix+=sample;
        }
        const auto value=short(std::clamp(mix,-32768,32767));
        out[i*2]=value;out[i*2+1]=value;
    }
}
bool TttTrack::seek(qint64 target,const std::function<bool()> &cancelled) {
    if(cancelled()) return false;
    target=std::max(qint64(0),target);
    if(target<frame_) {impl_->reset();frame_=0;}
    std::array<short,BlockFrames*2> scratch;
    while(frame_<target) {
        if(cancelled()) return false;
        render(scratch.data(),int(std::min(qint64(BlockFrames),target-frame_)));
    }
    return true;
}
void TttTrack::mute(VoiceMask mask) {impl_->muted=mask;}
bool TttTrack::hasTaps() const {return impl_->capture;}
bool TttTrack::readTap(int channel,QVector<float> &left,QVector<float> &right) const {
    if(!hasTaps() || channel<0 || channel>=2) return false;
    left.fill(0,ScopeFrames);
    for(int i=0;i<ScopeFrames;++i) {
        const auto at=frame_-ScopeFrames+i;
        if(at>=0) left[i]=impl_->history[channel][at%ScopeFrames];
    }
    right=left;return true;
}
QStringList TttTrack::instrumentNames(qint64 frame) const {
    const auto &d=*impl_;const int at=d.tickAt(std::max(qint64(0),frame));
    QStringList names;
    for(int c=0;c<2;++c) {
        const int ins=at<0?-1:d.song.ticks[at].instruments[c];
        names<<d.song.instruments.value(ins);
    }
    return names;
}
