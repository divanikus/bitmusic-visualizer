#include "window.h"
#include "romfiles.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
void write(const QString &path,const QByteArray &data) { QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write sample fixture."); }
QVector<short> render(MusicTrack &t,int n=1024) { QVector<short> p(n*2);t.render(p.data(),n);return p; }
template<class T> double rms(const T &v) { double sum=0;for(auto x:v)sum+=double(x)*x;return std::sqrt(sum/std::max<qsizetype>(1,v.size())); }
// Original synthetic nibble patterns, including the pretend rhythm ROM. No game/ROM data.
QByteArray sample(int size,int period=8) { QByteArray out(size,'\0');for(int i=0;i<size;++i)out[i]=char((i/period)%2?0x99:0x11);return out; }
struct Vgm {
    QByteArray bytes = QByteArray(0x100,'\0');
    int id, op, ticks=0, loopAt=0, loopTick=0;
    explicit Vgm(int chip,bool dual=false,bool variant=false):id(chip),op(chip==7?0x56:chip==8?0x58:0x5c) {
        bytes.replace(0,4,"Vgm ");word(8,0x171);word(0x34,0xcc);
        word(0x44+(id-6)*4,(id==11?3579545u:8000000u)|(dual?0x40000000u:0)|(variant?0x80000000u:0));
        for(int c=0;c<(dual?2:1);++c)if(id!=11)reg(7,0x3f,c);
    }
    void word(int p,quint32 v){qToLittleEndian(v,bytes.data()+p);}
    void reg(int address,int value,int chip=0){bytes+=char(op+chip*0x50+(address>>8));bytes+=char(address);bytes+=char(value);}
    void wait(int n){ticks+=n;while(n){int step=std::min(n,65535);bytes+=char(0x61);bytes+=char(step);bytes+=char(step>>8);n-=step;}}
    void loop(){loopAt=bytes.size();loopTick=ticks;}
    void data(int type,const QByteArray &value,int chip=0,quint32 total=0,quint32 offset=0) {
        bytes+=char(0x67);bytes+=char(0x66);bytes+=char(type);
        const int p=bytes.size();bytes+=QByteArray(12,'\0');
        word(p,quint32(value.size()+8)|(chip?0x80000000u:0));word(p+4,total?total:quint32(value.size()));word(p+8,offset);bytes+=value;
    }
    QByteArray finish(){bytes+=char(0x66);word(4,bytes.size()-4);word(0x18,ticks);if(loopAt){word(0x1c,loopAt-0x1c);word(0x20,ticks-loopTick);}return bytes;}
    void delta(int chip=0,int pan=0xc0,bool cpuUpload=false) {
        const int base=id==7?0x100:id==8?0x10:7;
        reg(base,1,chip);reg(base+1,pan|1,chip); // ROM addressing, 32-byte units (2610: 256).
        reg(base+2,0,chip);reg(base+3,0,chip);reg(base+4,id==8?15:127,chip);reg(base+5,0,chip);
        if(id!=8){reg(base+12,0xff,chip);reg(base+13,0xff,chip);}
        reg(base+9,0,chip);reg(base+10,0x80,chip);reg(base+11,255,chip);
        if(cpuUpload){reg(base,0x60,chip);for(auto b:sample(4096))reg(base+8,quint8(b),chip);}
        reg(base,0xb0,chip); // External playback with repeat.
    }
    void rhythm(bool internal,int pan=0x80) {
        const int base=internal?0x10:0x100;
        reg(base+1,63);reg(base+8,pan|31);
        if(!internal){reg(base+0x10,0);reg(base+0x18,0);reg(base+0x20,15);reg(base+0x28,0);}
        reg(base,1);
    }
    void fm(int channel) {
        for(int slot=0;slot<4;++slot){int s=slot*4+channel;reg(0x30+s,1);reg(0x40+s,slot==3?0:127);reg(0x50+s,31);reg(0x60+s,0);reg(0x70+s,0);reg(0x80+s,15);}
        reg(0xb0+channel,7);reg(0xb4+channel,0x80);reg(0xa4+channel,0x22);reg(0xa0+channel,0x69);reg(0x28,0xf0|channel);
    }
};
void aligned(MusicTrack &t,const QVector<short> &pcm) {
    QVector<float> l,r;QVector<int> sum(pcm.size(),0);
    for(int c=0;c<t.info().voices.size();++c){require(t.readTap(c,l,r),"Sample tap missing.");for(int i=0;i<pcm.size()/2;++i){sum[2*i]+=int(l[ScopeFrames-pcm.size()/2+i]);sum[2*i+1]+=int(r[ScopeFrames-pcm.size()/2+i]);}}
    for(int i=0;i<pcm.size();++i)require(std::abs(pcm[i]-std::clamp(sum[i],-32768,32767))<=32,"Sample PCM/tap mismatch.");
}
void fails(const QString &path,const QByteArray &bytes,const char *message) {
    write(path,bytes);QString error;try{auto t=openMusicTrack(path);render(*t,SampleRate);}catch(const std::exception &e){error=QString::fromUtf8(e.what());}
    require(error.contains(QString::fromLatin1(message)),"Sample rejection reason incorrect.");
}
void until(QApplication &app,const std::function<bool()> &ready,const char *message) {
    QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<12000){app.processEvents();QThread::msleep(4);}require(ready(),message);
}
}

void checkYamahaSamples(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    QTemporaryDir temp(QDir(directory).filePath("yamaha-samples-XXXXXX"));require(temp.isValid(),"No sample fixture directory.");
    const auto path=temp.filePath("samples.vgm"),romPath=temp.filePath("ymfm/ym2608_adpcm_rom.bin");
    const auto old=app.property("romsDirectoryForTests");app.setProperty("romsDirectoryForTests",temp.path());
    struct Restore {QApplication &app;QVariant value;~Restore(){app.setProperty("romsDirectoryForTests",value);}} restore{app,old};
    for(int id:{7,8,11})for(bool dual:{false,true}) {
        Vgm v(id,dual);for(int c=0;c<(dual?2:1);++c){v.data(id==7?0x81:id==8?0x83:0x88,sample(4096,8+4*c),c);v.delta(c,c?0x40:0x80);}
        v.loop();v.wait(SampleRate);write(path,v.finish());
        auto t=openMusicTrack(path,0,true,true),reference=openMusicTrack(path);t->seek(15000);reference->seek(15000);
        auto pcm=render(*t);require(rms(pcm)>10&&pcm==render(*reference),"Delta-T samples silent or capture changes sound.");aligned(*t,pcm);
        QVector<float> l,r;t->readTap((id==7?15:id==8?13:9),l,r);require(rms(l)>10&&(id==11?l==r:rms(r)==0),"Delta-T routing incorrect.");
        const double mean=std::accumulate(l.begin(),l.end(),0.0)/l.size();int crossings=0;
        for(int i=1;i<l.size();++i)if(l[i-1]<=mean&&l[i]>mean)++crossings;
        const double pitch=double(crossings)*SampleRate/l.size();
        const double expected=(id==11?3579545.0/72:8000000.0/144)*.5/32;
        require(std::abs(pitch-expected)<expected*.05,"Delta-T clock/pitch mismatch.");
        if(dual){QVector<float> l2,r2;t->readTap((id==7?31:id==8?27:19),l2,r2);require(rms(r2)>10&&r2!=l,"Second sample chip memory/panning incorrect.");}
        t->mute(~VoiceMask{});require(rms(render(*t))==0,"Sample mute-all failed.");render(*reference);
        t->mute(0);require(render(*t)==render(*reference),"Sample mute changed emulation state.");
        for(int target:{5*SampleRate,12000,0,3*SampleRate}){t->seek(target);reference->seek(target);pcm=render(*t);require(pcm==render(*reference),"Sample loop/seek mismatch.");aligned(*t,pcm);}
        require(!t->seek(3600LL*SampleRate,[]{return true;}),"Sample seek cancellation ignored.");
        log<<"Yamaha samples "<<id<<(dual?" dual":" single")<<": Delta-T, L/R, PCM/taps, mute, rewind/loop PASS\n";log.flush();
    }
    // OPNA FM needs no rhythm ROM. OPNB's two absent FM channels must not masquerade as 2610B.
    for(int id:{7,8}){Vgm v(id);v.fm(1);v.wait(SampleRate);write(path,v.finish());auto t=openMusicTrack(path);t->seek(10000);require(rms(render(*t))>10,"OPNA/OPNB FM is silent.");}
    QString standardId;
    for(bool variant:{false,true}){Vgm v(8,false,variant);v.fm(0);v.wait(SampleRate);write(path,v.finish());auto t=openMusicTrack(path);t->seek(10000);
        require(variant?rms(render(*t))>10:rms(render(*t))==0,"YM2610B extra FM channel selection incorrect.");
        if(!variant)standardId=t->info().configuration.id;else require(t->info().configuration.id!=standardId&&t->info().configuration.name=="YM2610B","YM2610B identity incorrect.");}
    // Separate ADPCM-A bank, left-only, and sparse blocks at their actual stream positions.
    Vgm a(8);a.data(0x82,sample(2048),0,4096,0);a.data(0x82,sample(2048),0,4096,2048);a.rhythm(false);a.wait(6000);write(path,a.finish());
    auto adpcmA=openMusicTrack(path,0,true,true);auto apcm=render(*adpcmA,4096);require(rms(apcm)>10,"YM2610 ADPCM-A is silent.");aligned(*adpcmA,apcm);
    for(int i=1;i<apcm.size();i+=2)require(apcm[i]==0,"ADPCM-A pan ignored.");
    Vgm initial(8);initial.data(0x83,sample(4096));initial.delta();initial.wait(10000);
    auto changed=initial;changed.data(0x83,sample(4096,23));changed.wait(10000);initial.wait(10000);
    write(path,initial.finish());auto before=openMusicTrack(path);write(path,changed.finish());auto after=openMusicTrack(path);
    require(render(*before,10000)==render(*after,10000),"Future sample block changed earlier audio.");
    require(render(*before,10000)!=render(*after,10000),"Timed sample block was ignored.");
    after->seek(0);write(path,changed.bytes);auto fresh=openMusicTrack(path);require(render(*after,20000)==render(*fresh,20000),"Rewind kept future sample RAM.");
    // Delta-T uploads through chip registers, with no pre-existing data block.
    for(int id:{7,11}){Vgm v(id);v.delta(0,0xc0,true);v.wait(SampleRate);write(path,v.finish());auto t=openMusicTrack(path);t->seek(15000);require(rms(render(*t))>10,"CPU-written Delta-T RAM is silent.");}
    Vgm missing(8);missing.delta();missing.wait(SampleRate);fails(path,missing.finish(),"sample memory is missing");
    Vgm bad(8);bad.data(0x83,sample(32),0,16);bad.wait(100);fails(path,bad.finish(),"sample-memory range");
    Vgm huge(8);huge.data(0x83,{},0,0x1000001);huge.wait(100);fails(path,huge.finish(),"sample-memory range");
    Vgm undeclared(8);undeclared.data(0x83,sample(32),1);undeclared.wait(100);fails(path,undeclared.finish(),"undeclared");
    Vgm shortBlock(8);shortBlock.data(0x83,sample(32));shortBlock.wait(100);auto truncated=shortBlock.finish();
    const auto marker=truncated.indexOf(QByteArray::fromHex("676683"));qToLittleEndian<quint32>(0x7fffffffu,truncated.data()+marker+3);fails(path,truncated,"Truncated VGM sample block");
    // ROM requests are delayed until a genuine rhythm read. Byte snapshots survive removal and rewind.
    Vgm rhythm(7);rhythm.wait(12000);rhythm.loop();rhythm.rhythm(true);rhythm.wait(12000);const auto rhythmBytes=rhythm.finish();
    write(path,rhythmBytes);auto absent=openMusicTrack(path);require(rms(render(*absent,11000))==0,"Unused rhythm ROM was required.");
    fails(path,rhythmBytes,"YM2608 ROM required:");require(QDir().mkpath(temp.filePath("ymfm")),"Cannot make test ROM directory.");
    write(romPath,QByteArray(4,0));fails(path,rhythmBytes,"8192 bytes");write(romPath,sample(8192));write(path,rhythmBytes);
    auto present=openMusicTrack(path,0,true,true);require(QFile::remove(romPath),"Cannot remove test ROM.");
    auto scope=openMusicTrack(path,0,false,true,{},present->info().yamahaRoms);
    present->seek(12500);scope->seek(12500);auto rhythmPcm=render(*present);require(rms(rhythmPcm)>10&&rhythmPcm==render(*scope),"Rhythm ROM snapshot differs across workers.");aligned(*present,rhythmPcm);
    present->seek(0);scope->seek(0);require(render(*present,18000)==render(*scope,18000),"Rhythm ROM snapshot lost on rewind.");
    fails(path,rhythmBytes,"File is missing");
    log<<"Yamaha sample memory: ADPCM-A, timed/sparse blocks, CPU uploads, FM/2610B, validation and on-demand ROM snapshots PASS\n";log.flush();
    if(decoderOnly)return;
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    until(app,[&]{window.refresh();return window.findChild<QMessageBox*>("romWarning");},"YM2608 ROM warning not shown.");
    auto warning=window.findChild<QMessageBox*>("romWarning");require(warning->text().contains("ym2608_adpcm_rom.bin")&&warning->text().contains(QDir::toNativeSeparators(romPath)),"YM2608 ROM path missing.");
    warning->accept();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);window.refresh();require(!window.findChild<QMessageBox*>("romWarning"),"ROM warning repeated.");
    Vgm playable(8,true);for(int c=0;c<2;++c){playable.data(0x83,sample(4096,8+4*c),c);playable.delta(c,c?0x40:0x80);}playable.loop();playable.wait(2*SampleRate);write(path,playable.finish());window.loadFile(path);
    auto ready=[&]{auto s=window.player().state();auto f=window.player().scopes();return s.valid&&!s.busy&&s.error.isEmpty()&&s.sharedScopes&&f.mask==VoiceMask::first(28)&&f.generation==s.generation;};
    until(app,ready,"Sample playback did not recover from ROM warning.");window.player().toggleSolo(27);window.player().setOutputScopesEnabled(true);window.player().seek(500,true);
    until(app,[&]{return ready()&&window.player().outputScopes().sequence;},"Sample Full mix missing.");
    window.player().seek(750,false);until(app,ready,"Sample paused seek lost chip scopes.");
    window.player().setRepeat(true);window.player().seek(1900,true);const auto generation=window.player().state().generation;
    until(app,[&]{return ready()&&window.player().state().generation>generation;},"Sample repeat stalled.");
    require(!window.player().state().starvations&&!window.player().state().outputErrors,"Sample playback audio errors.");window.close();window.player().shutdown();
    log<<"Yamaha samples live: ROM warning/recovery, chip Solo, Full mix, seek/repeat and zero audio errors PASS\n";log.flush();
}
