#include "window.h"
#include "vgm.h"
#include "ymfm-voices/voices.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QElapsedTimer>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QSettings>
#include <QListWidget>
#include <QtEndian>
#include <cmath>
#include <limits>

namespace {
void require(bool ok,const char *text) { if(!ok) throw std::runtime_error(text); }
void write(const QString &path,const QByteArray &bytes) { QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Cannot write voice test fixture."); }
template<class T> double energy(const T &pcm) { double sum=0; for(auto v:pcm)sum+=double(v)*v; return sum/std::max<qsizetype>(1,pcm.size()); }
QVector<short> render(MusicTrack &t,int n=1024) { QVector<short> out(n*2);t.render(out.data(),n);return out; }
uint8_t sampleByte(int at) { return uint8_t((at/7)%2?0x99:0x11); }
template<class Write> void configure(Write reg,int id,int algorithm=7,int transpose=0) {
    if(id==3) {
        for(int c=0;c<8;++c) {
            for(int op=0;op<4;++op) {
                const int s=op*8+c;
                reg(0x40+s,1+op);reg(0x60+s,12+op*3);reg(0x80+s,31);
                reg(0xa0+s,0);reg(0xc0+s,0);reg(0xe0+s,15);
            }
            reg(0x20+c,(c%2?0x80:0x40)|0x38|algorithm);reg(0x28+c,0x30+c*3+transpose);
            reg(0x30+c,c*7);reg(8,0x78|c);
        }
    } else if(id==6||id==7||id==8) {
        if(id==7)reg(0x29,0x80);
        for(int c=0;c<(id==6?3:6);++c) {
            int bank=c<3?0:0x100,ch=c%3;
            for(int op=0;op<4;++op) {
                const int s=bank+op*4+ch;
                reg(0x30+s,1+op);reg(0x40+s,12+op*3);reg(0x50+s,31);
                reg(0x60+s,0);reg(0x70+s,0);reg(0x80+s,15);
            }
            reg(bank+0xb0+ch,0x38|algorithm);reg(bank+0xb4+ch,c%2?0x40:0x80);
            reg(bank+0xa4+ch,0x22);reg(bank+0xa0+ch,65+c*29+transpose);reg(0x28,0xf0|ch|(c>=3?4:0));
        }
        reg(7,0x38);
        for(int c=0;c<3;++c){reg(c*2,70+c*53+transpose);reg(c*2+1,0);reg(8+c,12+c);}
        if(id!=6) {
            const int a=id==7?0x10:0x100;
            reg(a+1,63);
            for(int c=0;c<6;++c) {
                reg(a+8+c,(c%2?0x40:0x80)|31);
                if(id==8){reg(a+0x10+c,c*2);reg(a+0x18+c,0);reg(a+0x20+c,c*2+1);reg(a+0x28+c,0);}
            }
            reg(a,63);
        }
    } else {
        if(id==12)reg(0x105,1);
        for(int c=0;c<(id==12?18:9);++c) {
            const int bank=c/9*256,ch=c%9,slot=bank+(ch/3)*8+ch%3;
            for(int op:{slot,slot+3}){reg(0x20+op,0x21);reg(0x40+op,8);reg(0x60+op,0xf0);reg(0x80+op,7);}
            reg(bank+0xc0+ch,(id==12?(1<<(4+c%4)):0)|0xe|(algorithm&1));
            reg(bank+0xa0+ch,50+c*9+transpose);reg(bank+0xb0+ch,0x31);
        }
    }
    if(id==7||id==8||id==11) {
        const int b=id==7?0x100:id==8?0x10:7;
        reg(b,1);reg(b+1,0xc1);reg(b+2,0);reg(b+3,0);
        reg(b+4,id==8?15:127);reg(b+5,0);
        if(id!=8){reg(b+12,255);reg(b+13,255);}
        reg(b+9,0);reg(b+10,0x80);reg(b+11,255);reg(b,0xb0);
    }
}
template<class Core> struct Harness : ymfm::ymfm_interface {
    Core core;
    int divider;
    qint64 now=0;
    std::array<qint64,2> timers{INT64_MAX,INT64_MAX};
    Harness():core(*this) {
        if constexpr(std::is_base_of_v<ymfm::ym2608,Core>||std::is_base_of_v<ymfm::ym2610,Core>) core.set_fidelity(ymfm::OPN_FIDELITY_MIN);
        core.reset();divider=72000000/core.sample_rate(72000000);
    }
    void write(int r,int v){core.write(2*(r>>8),r&255);core.write(2*(r>>8)+1,v);}
    uint8_t ymfm_external_read(ymfm::access_class,uint32_t at) override{return sampleByte(at);}
    void ymfm_set_timer(uint32_t t,int32_t duration) override{timers[t]=duration<0?INT64_MAX:now+std::max(1,duration);}
    void clock() {
        const auto target=now+divider;
        while(std::min(timers[0],timers[1])<=target) {
            now=std::min(timers[0],timers[1]);
            for(int t=0;t<2;++t)if(timers[t]==now){timers[t]=INT64_MAX;m_engine->engine_timer_expired(t);}
        }
        now=target;
    }
};
template<class Core> void checkOplGroups(int id) {
    const int drumBits[]={16,1,8,4,2}, groups[]={6,7,7,8,8};
    for(int drum=0;drum<5;++drum) {
        Harness<Core> chip;
        // Set timbres without starting melodic envelopes.
        configure([&](int r,int v) {chip.write(r,((r&255)>=0xb0&&(r&255)<=0xb8)?v&~0x20:v);},id);
        chip.write(0xbd,0x20|drumBits[drum]);
        std::array<int64_t,3> energies{};
        for(int n=0;n<2048;++n) {
            chip.clock();bitmusic::VoiceSample sample;chip.core.generate_voices(sample);
            for(int c=6;c<9;++c) for(int b=0;b<4;++b) energies[c-6]+=std::abs(sample.voice(c,b));
        }
        for(int c=6;c<9;++c) require((energies[c-6]>0)==(c==groups[drum]),"OPL drum appears on the wrong hardware card.");
    }
    if constexpr(std::is_same_v<Core,bitmusic::Ymf262Voices>) {
        Harness<Core> chip;configure([&](int r,int v){chip.write(r,v);},id);
        chip.write(0x104,63);
        std::array<int64_t,18> energies{};
        for(int n=0;n<2048;++n) {
            chip.clock();bitmusic::VoiceSample sample;chip.core.generate_voices(sample);
            for(int c=0;c<18;++c)for(int b=0;b<4;++b)energies[c]+=std::abs(sample.voice(c,b));
        }
        for(int c=0;c<18;++c)
            require((energies[c]>0)==(c%9<3||c%9>=6),"OPL3 paired voice appears twice or loses its output.");
    }
}
template<class Original,class Tapped> void compareCore(int id,QTextStream &log) {
    constexpr bool opn=std::is_base_of_v<ymfm::ym2608,Original>||std::is_base_of_v<ymfm::ym2610,Original>;
    constexpr bool opl3=std::is_same_v<Original,ymfm::ymf262>;
    for(int algorithm=0;algorithm<8;++algorithm) {
        Harness<Original> reference;Harness<Tapped> tapped;
        auto both=[&](int r,int v){reference.write(r,v);tapped.write(r,v);};configure(both,id,algorithm);
        for(int n=0;n<10000;++n) {
            if(n==2000&&id>=9){both(0xbd,0x3f);if(id==12)both(0x104,0x3f);}
            if(n==4000&&id>=9){both(0xbd,0);if(id==12)both(0x104,0);}
            if(n==2000&&id==3){both(0xf,0x9f);both(0x18,0xff);both(0x19,0x7f);}
            if(n==5000&&id==7)both(0x2e,0);
            if(n==6000&&id==7)both(0x2f,0);
            if(n==7000&&opn){both(7,0x0b);both(6,7);both(11,40);both(13,10);for(int c=0;c<3;++c)both(8+c,16);}
            if(n==8000&&id==7){both(0x2d,0);both(0x29,0);both(0x27,0x40);}
            reference.clock();tapped.clock();
            typename Original::output_data expected;reference.core.generate(&expected);
            bitmusic::VoiceSample actual;tapped.core.generate_voices(actual);
            auto mixed=actual.mix(~0u,18,(opn||opl3)?bitmusic::VoiceSample::Dac::Clamp16:bitmusic::VoiceSample::Dac::Floating);
            for(unsigned bus=0;bus<(opn?2:Original::OUTPUTS);++bus)
                require(mixed[bus]==expected.data[bus],"Yamaha separated output differs from upstream core.");
            if constexpr(opn)require(actual.ssgMix(~0u)==expected.data[2],"Yamaha separated SSG resampler changes the mix.");
        }
    }
    log<<"Yamaha core "<<id<<": all algorithms, DAC/clipping, stereo buses, noise/rhythm/4-op/prescalers and SSG are bit-exact PASS\n";log.flush();
}
struct Vgm {
    QByteArray bytes=QByteArray(0x100,'\0');
    void word(int p,quint32 v){qToLittleEndian(v,bytes.data()+p);}
    Vgm(){bytes.replace(0,4,"Vgm ");word(8,0x171);word(0x34,0xcc);}
    void chip(int id,bool dual,bool variant=false) {
        word(id==3?0x30:0x44+(id-6)*4,(id==6?4000000u:id==7||id==8?8000000u:id==12?14318180u:3579545u)|(dual?0x40000000u:0)|(variant?0x80000000u:0));
        for(int instance=0;instance<(dual?2:1);++instance) {
            auto data=[&](int type) {
                bytes+=char(0x67);bytes+=char(0x66);bytes+=char(type);
                const int at=bytes.size();bytes+=QByteArray(12,'\0');
                word(at,4104u|(instance?0x80000000u:0));word(at+4,4096);
                for(int n=0;n<4096;++n)bytes+=char(sampleByte(n));
            };
            if(id==7)data(0x81);
            if(id==8){data(0x82);data(0x83);}
            if(id==11)data(0x88);
            const int op=id==3?0x54:id==6?0x55:id==7?0x56:id==8?0x58:id==9?0x5a:id==10?0x5b:id==11?0x5c:0x5e;
            configure([&](int r,int v){bytes+=char(op+instance*0x50+(r>>8));bytes+=char(r);bytes+=char(v);},id,7,instance*9);
        }
    }
    QByteArray finish() {const int loop=bytes.size();for(int n=0;n<120;++n)bytes+=char(0x62);bytes+=char(0x66);
        word(4,bytes.size()-4);word(0x18,88200);word(0x1c,loop-0x1c);word(0x20,88200);return bytes;}
};
void until(QApplication &app,const std::function<bool()> &ready,const char *message) {
    QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<15000){app.processEvents();QThread::msleep(4);}require(ready(),message);
}
}

void checkAllYamahaVoices(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    compareCore<ymfm::ym2151,bitmusic::Ym2151Voices>(3,log);
    compareCore<ymfm::ym3526,bitmusic::Ym3526Voices>(10,log);
    compareCore<ymfm::ym3812,bitmusic::Ym3812Voices>(9,log);
    compareCore<ymfm::y8950,bitmusic::Y8950Voices>(11,log);
    compareCore<ymfm::ymf262,bitmusic::Ymf262Voices>(12,log);
    compareCore<ymfm::ym2608,bitmusic::Ym2608Voices>(7,log);
    compareCore<ymfm::ym2610,bitmusic::Ym2610Voices>(8,log);
    compareCore<ymfm::ym2610b,bitmusic::Ym2610bVoices>(8,log);
    checkOplGroups<bitmusic::Ym3526Voices>(10);
    checkOplGroups<bitmusic::Ym3812Voices>(9);
    checkOplGroups<bitmusic::Y8950Voices>(11);
    checkOplGroups<bitmusic::Ymf262Voices>(12);
    log<<"OPL: all five isolated drums use the three hardware groups; OPL3 four-operator pairs occupy only the first member PASS\n";log.flush();
    QTemporaryDir temp(QDir(directory).filePath("all-yamaha-voices-XXXXXX"));require(temp.isValid(),"No Yamaha voice test directory.");
    const auto previous=app.property("romsDirectoryForTests");app.setProperty("romsDirectoryForTests",temp.path());
    struct Restore {QApplication &app;QVariant value;~Restore(){app.setProperty("romsDirectoryForTests",value);}} restore{app,previous};
    QDir().mkpath(temp.filePath("ymfm"));QByteArray rom(8192,'\0');for(int n=0;n<rom.size();++n)rom[n]=char(sampleByte(n));write(temp.filePath("ymfm/ym2608_adpcm_rom.bin"),rom);
    const auto path=temp.filePath("voices.vgm");
    for(const auto &spec : {std::pair{3,false},std::pair{7,false},std::pair{8,false},std::pair{8,true},
                          std::pair{9,false},std::pair{10,false},std::pair{11,false},std::pair{12,false}})
    for(bool dual:{false,true}) {
        const int id=spec.first; const bool variant=spec.second;
        Vgm v;v.chip(id,dual,variant);write(path,v.finish());
        auto t=openMusicTrack(path,0,true,true),reference=openMusicTrack(path,0,true,true);
        const int count=(dual?2:1)*(id==3?8:id==7?16:id==8?(variant?16:14):id==12?18:id==11?10:9);
        require(t->info().voices.size()==count&&t->info().configuration.sources.size()==count,"Yamaha voice counts/identities incorrect.");
        t->seek(6000);reference->seek(6000);
        QVector<float> l,r,rl,rr;
        for(int c=0;c<count;++c) {
            // Internal one-shot rhythm samples can finish in a few milliseconds.
            t->seek(0);reference->seek(0);t->seek(256);reference->seek(256);
            if(!t->readTap(c,l,r)||energy(l)+energy(r)<=1)
                throw std::runtime_error(QString("Yamaha %1 voice %2 is missing/silent.").arg(id).arg(c).toStdString());
            t->mute(~VoiceMask::bit(c));const auto solo=render(*t,127);render(*reference,127);
            // Rhythms may naturally end while the test walks through the voices.
            t->readTap(c,l,r);reference->readTap(c,rl,rr);require(l==rl&&r==rr,"Yamaha Solo disturbed voice state.");
            for(int n=0;n<127;++n) {
                require(std::abs(solo[2*n]-l[ScopeFrames-127+n])<=64&&std::abs(solo[2*n+1]-r[ScopeFrames-127+n])<=64,"Yamaha Solo/tap timing or mapping mismatch.");
            }
        }
        t->mute(~VoiceMask{});require(energy(render(*t))==0,"Yamaha mute-all leaks resampler history.");render(*reference);
        t->mute(0);require(render(*t)==render(*reference),"Yamaha unmute changes chip state.");
        for(int target:{5*SampleRate,7301,0}){t->seek(target);reference->seek(target);require(render(*t)==render(*reference),"Yamaha voice seek/loop mismatch.");}
        log<<"Yamaha "<<id<<(variant?"B":"")<<(dual?" dual":" single")<<": "<<count<<" voices, real signals, Solo alignment, state and transport PASS\n";log.flush();
    }
    // Cross the 32- and 64-bit boundaries without inventing an unsupported chip.
    Vgm many;many.chip(3,true);many.chip(9,true);many.chip(12,true);write(path,many.finish());
    auto manyTrack=openMusicTrack(path,0,true,true);require(manyTrack->info().voices.size()==70,"Wide Yamaha configuration incorrect.");
    manyTrack->seek(10000);manyTrack->mute(~VoiceMask::bit(69));require(energy(render(*manyTrack))>1,"Voice 70 Solo failed.");
    QVector<float> l,r;require(manyTrack->readTap(69,l,r)&&energy(l)+energy(r)>1,"Voice 70 capture missing.");
    log<<"Yamaha 70-voice mixed configuration: high-bit mute/Solo/capture PASS\n";log.flush();
    ScopePreset preset;preset.configuration=manyTrack->info().configuration;preset.rows=14;preset.columns=5;preset.stereo=true;
    for(const auto &source:preset.configuration.sources)preset.cards.push_back({source});
    preset.cards.push_back({"mix"});QString error;
    require(preset.save(temp.filePath("layout.ini"),error),"Wide preset save failed.");
    ScopePreset loaded;require(loaded.load(temp.filePath("layout.ini"),error)&&loaded.encode()==preset.encode(),"Wide preset roundtrip failed.");
    ScopeTheme theme;theme.tiles[69].waveform=QColor("#ABCDEF");theme.fullMix.waveform=QColor("#123456");
    require(theme.save(temp.filePath("theme.ini"),error),"Wide theme save failed.");
    ScopeTheme colors;require(colors.load(temp.filePath("theme.ini"),error)&&colors.tiles[69].waveform==theme.tiles[69].waveform&&colors.fullMix.waveform==theme.fullMix.waveform,"Wide colors or Full mix alias.");
    {QSettings ini(temp.filePath("theme.ini"),QSettings::IniFormat);ini.setValue("Theme/Slots",32);}
    require(colors.load(temp.filePath("theme.ini"),error)&&colors.tiles[69].waveform==theme.tiles[5].waveform,"Legacy theme extension failed.");
    Vgm maximum;for(int id:{3,6,7,8,9,10,11,12})maximum.chip(id,true,id==8);
    const auto maxPath=temp.filePath("maximum.vgm");write(maxPath,maximum.finish());
    auto maxTrack=openMusicTrack(maxPath,0,true,true);
    require(maxTrack->info().voices.size()==184,"Maximum supported Yamaha configuration incomplete.");
    QElapsedTimer benchmark;benchmark.start();maxTrack->seek(SampleRate);
    maxTrack->mute(~VoiceMask::bit(183));require(energy(render(*maxTrack))>1&&maxTrack->readTap(183,l,r),"Last Yamaha voice aliases or disappears.");
    maxTrack->mute(~VoiceMask{});require(energy(render(*maxTrack))==0,"High voices escape mute-all.");
    log<<"Yamaha 184 voices: all supported dual chips, last source and mute-all PASS; one second decoded with capture in "<<benchmark.elapsed()<<" ms\n";
    log<<"Wide presets/themes and legacy 32-slot theme extension PASS\n";log.flush();
    if(decoderOnly)return;
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    auto ready=[&]{auto s=window.player().state();auto f=window.player().scopes();return s.valid&&!s.busy&&s.error.isEmpty()&&f.generation==s.generation&&f.mask.test(0);};
    until(app,ready,"Wide Yamaha live scopes missing.");window.refresh();
    auto lastButton=window.findChild<QToolButton*>("voice69");require(lastButton,"High channel button missing.");
    lastButton->click();require(window.player().state().muteMask.test(69)&&!window.player().state().muteMask.test(5),"High UI mute aliases low voice.");
    window.player().toggleSolo(69);require(window.player().state().muteMask==~VoiceMask::bit(69),"Wide live Solo incorrect.");
    window.scopeWindow().grab().save(QDir(directory).filePath("native-yamaha-wide.png"));
    window.player().setScopeMask(VoiceMask::bit(69));window.player().seek(250,false);
    until(app,[&]{auto s=window.player().state();auto f=window.player().scopes();return !s.busy&&f.generation==s.generation&&f.mask==VoiceMask::bit(69);},"High live capture lost after seek.");
    window.refresh();window.close();window.player().shutdown();
    log<<"Yamaha wide live UI: load, high voice Solo and paused capture PASS\n";log.flush();
}
