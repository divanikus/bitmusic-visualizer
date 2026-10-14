#include "window.h"
#include "ymfm-voices/ym2203.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <future>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
void write(const QString &path,const QByteArray &data) { QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write YM2203 voice fixture."); }
QVector<short> render(MusicTrack &t,int frames=1024) { QVector<short> out(frames*2);t.render(out.data(),frames);return out; }
template<class T> double rms(const T &v) { double sum=0;for(auto x:v)sum+=double(x)*x;return std::sqrt(sum/std::max<qsizetype>(1,v.size())); }
template<class Write> void configure(Write reg,int algorithm=7,int fmMask=7,int transpose=0) {
    for(int c=0;c<3;++c) {
        for(int slot=0;slot<4;++slot) {
            const int s=slot*4+c;
            reg(0x30+s,1+slot);reg(0x40+s,(fmMask&(1<<c))?24+slot*4:127);
            reg(0x50+s,31);reg(0x60+s,0);reg(0x70+s,0);reg(0x80+s,15);
        }
        reg(0xb0+c,0x38|algorithm); // Feedback must stay identical when extracting a voice.
        reg(0xa4+c,0x22);reg(0xa0+c,70+c*55+transpose);reg(0x28,0xf0|c);
        const int period=67+c*43+transpose;
        reg(2*c,period);reg(2*c+1,0);reg(8+c,10+c);
    }
    reg(7,0x38); // Three tone-only PSG channels, no extra fake "noise" channel.
    reg(11,40);reg(12,0);reg(13,10);reg(6,7);
}
template<class Core> struct Harness : ymfm::ymfm_interface {
    static constexpr qint64 Never=std::numeric_limits<qint64>::max();
    qint64 now=0;std::array<qint64,2> timers{Never,Never};Core core;
    explicit Harness():core(*this){core.set_fidelity(ymfm::OPN_FIDELITY_MIN);core.reset();}
    void write(int reg,int value){core.write(0,reg);core.write(1,value);}
    void ymfm_set_timer(uint32_t index,int32_t duration) override {timers[index]=duration<0?Never:now+std::max(1,duration);}
    void clock() {
        const auto target=now+24;
        while(std::min(timers[0],timers[1])<=target) {
            now=std::min(timers[0],timers[1]);
            for(int i=0;i<2;++i)if(timers[i]==now){timers[i]=Never;m_engine->engine_timer_expired(i);}
        }
        now=target;
    }
};
void nativeReference(QTextStream &log) {
    bool dacDifference=false;
    // The reference is the unmodified pinned ymfm core. Silence other FM
    // channels there using total levels, not our output/mute implementation.
    for(int algorithm=0;algorithm<8;++algorithm)for(int mask=0;mask<8;++mask) {
        Harness<ymfm::ym2203> reference;Harness<bitmusic::Ym2203Voices> tapped;
        configure([&](int r,int v){reference.write(r,v);},algorithm,mask);
        configure([&](int r,int v){tapped.write(r,v);},algorithm);
        auto both=[&](int r,int v){reference.write(r,v);tapped.write(r,v);};
        for(int n=0;n<12000;++n) {
            if(n==1100)both(0x2e,0); // 1.5 FM samples per native output.
            if(n==2200)both(0x2f,0);
            if(n==3300)both(0x2d,0);
            if(n==4400){both(7,0x0b);both(8,16);both(9,16);both(10,16);} // Shared noise/envelope.
            if(n==5500){both(0xa8,90);both(0xac,0x1a);both(0xa9,180);both(0xad,0x22);both(0xaa,240);both(0xae,0x2a);both(0x27,0x40);}
            if(n==6600)for(int c=0;c<3;++c)both(0x28,c);
            if(n==7700){both(0x24,0xf0);both(0x25,0);both(0x27,0x85);} // Timer-driven CSM.
            reference.clock();tapped.clock();
            ymfm::ym2203::output_data expected;reference.core.generate(&expected);
            bitmusic::Ym2203Voices::Voices actual;tapped.core.generate_voices(actual);
            require(expected.data[0]==actual.fmMix[mask],"YM2203 voice mix changed upstream FM/DAC output.");
            for(int c=0;c<3;++c)require(expected.data[c+1]==actual.channels[c+3],"YM2203 voice capture changed upstream SSG output.");
            if(mask==7&&actual.channels[0]+actual.channels[1]+actual.channels[2]!=actual.fmMix[7])dacDifference=true;
        }
    }
    require(dacDifference,"FM reference never exercised shared DAC quantization.");
    log<<"YM2203 upstream reference: all algorithms and FM combinations, feedback, prescalers, PSG noise/envelope, channel-3 special mode and timer CSM are bit-exact PASS\n";log.flush();
}
QByteArray fixture(bool dual=false) {
    QByteArray data(0x100,'\0');data.replace(0,4,"Vgm ");
    auto word=[&](int at,quint32 value){qToLittleEndian(value,data.data()+at);};
    word(8,0x171);word(0x34,0xcc);word(0x44,4000000u|(dual?0x40000000u:0));
    auto reg=[&](int chip,int r,int v){data+=char(chip?0xa5:0x55);data+=char(r);data+=char(v);};
    for(int c=0;c<(dual?2:1);++c)configure([&](int r,int v){reg(c,r,v);},7,7,c*17);
    const int loopAt=data.size();
    for(int i=0;i<120;++i)data+=char(0x62);
    data+=char(0x66);word(4,data.size()-4);word(0x18,88200);word(0x1c,loopAt-0x1c);word(0x20,88200);
    return data;
}
void until(QApplication &app,const std::function<bool()> &ready,const char *message) {
    QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<12000){app.processEvents();QThread::msleep(4);}require(ready(),message);
}
}

void checkYamahaVoices(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    nativeReference(log);
    QTemporaryDir temp(QDir(directory).filePath("ym2203-voices-XXXXXX"));require(temp.isValid(),"No voice fixture directory.");const auto path=temp.filePath("voices.vgm");
    for(bool dual:{false,true}) {
        write(path,fixture(dual));auto track=openMusicTrack(path,0,true,true),reference=openMusicTrack(path,0,true,true);
        const int count=dual?12:6;require(track->info().voices.size()==count&&!track->info().stereoOutput,"YM2203 voice count/mono incorrect.");
        const auto sources=track->info().configuration.sources;
        require(sources.size()==count&&sources[0]=="ym2203.0.fm:0"&&sources[5]=="ym2203.0.ssg:2"&&(!dual||sources[11]=="ym2203.1.ssg:2"),"YM2203 voice preset identities incorrect.");
        track->seek(22050);reference->seek(22050);
        QVector<QVector<float>> traces;QVector<float> left,right,refLeft,refRight;
        for(int c=0;c<count;++c) {
            require(track->readTap(c,left,right)&&left==right&&rms(left)>10,"YM2203 voice silent or not mono.");
            for(const auto &other:traces)require(other!=left,"YM2203 voices duplicate one another.");
            traces<<left;
        }
        const auto badSize=track->readTap(count,left,right);require(!badSize,"Out-of-range YM2203 tap accepted.");
        for(int c=0;c<count;++c) {
            track->mute(~(1u<<c));const auto solo=render(*track);render(*reference);
            require(rms(solo)>10,"YM2203 voice Solo is silent.");track->readTap(c,left,right);
            // PSG is directly additive; FM precedes the shared DAC. This low-level
            // fixture stays below clipping, making a small DAC error meaningful.
            for(int i=0;i<1024;++i)require(std::abs(solo[2*i]-left[ScopeFrames-1024+i])<=(c%6<3?32:0),"YM2203 solo/tap timing or channel mapping incorrect.");
            for(int v=0;v<count;++v){track->readTap(v,left,right);reference->readTap(v,refLeft,refRight);require(left==refLeft&&right==refRight,"YM2203 muting disturbed a running voice.");}
        }
        // Every combination, including mute-all, preserves state and leaves exact zero when muted.
        for(uint32_t mask=0;mask<64;++mask){track->mute(~mask);const auto pcm=render(*track,127);render(*reference,127);if(!mask)require(rms(pcm)==0,"YM2203 mute-all leaks FIR history.");}
        track->mute(0);require(render(*track)==render(*reference),"YM2203 unmute changed audio state.");
        for(int target:{5*SampleRate,7301,0,3*SampleRate}){track->seek(target);reference->seek(target);require(render(*track)==render(*reference),"YM2203 voices seek/loop mismatch.");}
        require(!track->seek(3600LL*SampleRate,[]{return true;}),"YM2203 voice seek cancellation ignored.");
        track->seek(0);auto chunked=openMusicTrack(path);const auto large=render(*track,8192);QVector<short> small;
        for(int n:{1,31,4096,7,1024,3033})small+=render(*chunked,n);
        require(large==small,"YM2203 chunk size changes output.");
        log<<"YM2203 "<<count<<" voices: distinct signals, real PCM alignment, all mutes/Solo, state preservation, seeks/loops and identities PASS\n";log.flush();
    }
    auto concurrent=[path]{auto t=openMusicTrack(path,0,true,true);t->seek(22050);return render(*t);};
    auto first=std::async(std::launch::async,concurrent),second=std::async(std::launch::async,concurrent);require(first.get()==second.get(),"YM2203 instances interfere.");
    write(path,fixture());auto perf=openMusicTrack(path,0,true,true);QElapsedTimer timer;timer.start();perf->seek(60LL*SampleRate);
    log<<"YM2203: 60 seconds with all six taps in "<<timer.elapsed()<<" ms\n";log.flush();
    if(decoderOnly)return;
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    auto ready=[&]{auto s=window.player().state();auto f=window.player().scopes();return s.valid&&!s.busy&&s.error.isEmpty()&&s.sharedScopes&&f.mask==63&&f.generation==s.generation&&std::abs(s.positionMs-f.positionMs)<200;};
    until(app,ready,"YM2203 live six-card scopes missing.");window.refresh();
    auto stereo=window.scopeWindow().findChild<QCheckBox*>("scopeStereo");require(stereo&&!stereo->isEnabled(),"Mono YM2203 stereo control enabled.");
    window.player().toggleSolo(4);require(window.player().state().muteMask==~VoiceMask::bit(4),"YM2203 PSG B Solo maps to the wrong voice.");
    window.player().setOutputScopesEnabled(true);window.player().seek(500,true);
    until(app,[&]{return ready()&&window.player().outputScopes().sequence;},"YM2203 Full mix missing.");
    window.player().seek(750,false);until(app,ready,"YM2203 paused seek lost voices.");
    window.refresh();window.scopeWindow().present(window.player().state(),window.player().scopes(),window.player().outputScopes());window.scopeWindow().grab().save(QDir(directory).filePath("native-ym2203-voices.png"));
    window.player().setRepeat(true);window.player().seek(1900,true);const auto generation=window.player().state().generation;
    until(app,[&]{return ready()&&window.player().state().generation>generation;},"YM2203 repeat lost voices.");
    window.scopeWindow().hide();until(app,[&]{return window.player().state().scopesSuspended;},"YM2203 hidden capture did not suspend.");window.scopeWindow().show();until(app,ready,"YM2203 voices did not resume.");
    require(!window.player().state().starvations&&!window.player().state().outputErrors,"YM2203 live audio errors.");window.close();window.player().shutdown();
    log<<"YM2203 live: six mono cards, PSG Solo, Full mix, paused seek, repeat, hide/resume and zero audio errors PASS\n";log.flush();
}
