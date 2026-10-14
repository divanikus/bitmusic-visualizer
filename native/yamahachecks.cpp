#include "window.h"
#include "vgm.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <numeric>
#include <stdexcept>

void checkYamahaSamples(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly);
void checkYamahaVoices(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly);

namespace {
void require(bool ok,const char *message) { if(!ok)throw std::runtime_error(message); }
void write(const QString &path,const QByteArray &data) { QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write Yamaha fixture."); }
QVector<short> block(MusicTrack &t,int n=1024) { QVector<short> p(n*2);t.render(p.data(),n);return p; }
template<class T> double rms(const T &v) { double sum=0;for(auto x:v)sum+=double(x)*x;return std::sqrt(sum/std::max<qsizetype>(1,v.size())); }
QByteArray fixture(int id,bool dual=false,bool loop=true,bool fm=false) {
    // Authored register patterns, no game music or ROMs.
    QByteArray data(0x100,'\0');data.replace(0,4,"Vgm ");
    auto word=[&](int p,quint32 n){qToLittleEndian(n,data.data()+p);};
    word(8,0x171);word(0x34,0x100-0x34);
    const int offset=id==3?0x30:0x44+(id-6)*4;
    const int op=id==3?0x54:id==6?0x55:id==7?0x56:id==8?0x58:id==9?0x5a:id==10?0x5b:id==11?0x5c:0x5e;
    const quint32 clock=id==6?4000000:id==7||id==8?8000000:id==12?14318180:3579545;
    word(offset,clock|(dual?0x40000000u:0));
    auto reg=[&](int chip,int address,int value){data+=char(op+chip*0x50+(address>>8));data+=char(address);data+=char(value);};
    for(int c=0;c<(dual?2:1);++c) {
        if(id>=6 && id<=8) {
            reg(c,7,0x3f); // SSG initially silent.
            if(!fm) {reg(c,0,80+40*c);reg(c,1,0);reg(c,7,0x3e);reg(c,8,15);}
            else {
                for(int slot=0;slot<4;++slot) {int s=slot*4;reg(c,0x30+s,1);reg(c,0x40+s,slot==3?0:127);reg(c,0x50+s,31);reg(c,0x60+s,0);reg(c,0x70+s,0);reg(c,0x80+s,15);}
                reg(c,0xb0,7);reg(c,0xa4,0x22);reg(c,0xa0,0x69);reg(c,0x28,0xf0);
            }
        } else if(id==3) {
            for(int slot=0;slot<4;++slot) {int s=slot*8;reg(c,0x40+s,1);reg(c,0x60+s,slot==3?0:127);reg(c,0x80+s,31);reg(c,0xa0+s,0);reg(c,0xc0+s,0);reg(c,0xe0+s,15);}
            reg(c,0x20,0x47);reg(c,0x28,0x4a+c);reg(c,0x30,0);reg(c,8,0x78);
        } else {
            if(id==12)reg(c,0x105,1);
            reg(c,0x20,0x21);reg(c,0x23,0x21);reg(c,0x40,63);reg(c,0x43,0);
            reg(c,0x60,0xf0);reg(c,0x63,0xf0);reg(c,0x80,7);reg(c,0x83,7);
            reg(c,0xc0,id==12?0x11:1);reg(c,0xa0,0x98+30*c);reg(c,0xb0,0x31);
        }
    }
    const int loopAt=data.size();
    for(int i=0;i<120;++i) {
        // Change routing during playback, including repeated loop iterations.
        if(id==3 && (i==0||i==60))reg(0,0x20,i==0?0x47:0x87);
        if(id==12 && (i==0||i==60))reg(0,0xc0,i==0?0x11:0x21);
        data+=char(0x62);
    }
    data+=char(0x66);word(4,data.size()-4);word(0x18,88200);
    if(loop){word(0x1c,loopAt-0x1c);word(0x20,88200);}
    return data;
}
void aligned(MusicTrack &t,const QVector<short> &pcm) {
    QVector<float> left,right;QVector<int> sum(pcm.size(),0);
    for(int c=0;c<t.info().voices.size();++c) {
        require(t.readTap(c,left,right)&&left.size()==ScopeFrames&&right.size()==ScopeFrames,"Yamaha chip mix missing.");
        for(int i=0;i<pcm.size()/2;++i) {sum[2*i]+=int(left[ScopeFrames-pcm.size()/2+i]);sum[2*i+1]+=int(right[ScopeFrames-pcm.size()/2+i]);}
    }
    // YM2203 FM taps precede its shared nonlinear DAC. Their exact native
    // signals and audible combinations are checked against upstream separately.
    const bool separateFm = t.info().configuration.sources.contains("ym2203.0.fm:0");
    if (!separateFm) for(int i=0;i<pcm.size();++i)require(pcm[i]==std::clamp(sum[i],-32768,32767),"Yamaha chip mixes differ from audible PCM.");
}
void until(QApplication &app,const std::function<bool()> &ready,const char *message) {
    QElapsedTimer t;t.start();while(!ready()&&t.elapsed()<12000){app.processEvents();QThread::msleep(4);}require(ready(),message);
}
void reject(const QString &path,const QByteArray &data,const char *text) {
    write(path,data);QString error;try{auto t=openMusicTrack(path);}catch(const std::exception &e){error=QString::fromUtf8(e.what());}
    require(error.contains(QString::fromLatin1(text)),"Yamaha invalid input error mismatch.");
}
}
void checkYamaha(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    QTemporaryDir temp(QDir(directory).filePath("yamaha-XXXXXX"));require(temp.isValid(),"No Yamaha fixture directory.");
    const auto path=temp.filePath("test.vgm");
    for(int id:{3,6,7,8,9,10,11,12})for(bool dual:{false,true}) {
        write(path,fixture(id,dual));auto t=openMusicTrack(path,0,true,true),reference=openMusicTrack(path);
        require(t->hasTaps()&&t->info().voices.size()==(dual?2:1)*(id==6?6:1)&&t->info().configuration.family=="yamaha-vgm","Yamaha capabilities incorrect.");
        require(t->info().stereoOutput==(id==3||id==7||id==8||id==12),"Yamaha mono/stereo capability incorrect.");
        QVector<short> pcm;
        for(int i=0;i<15;++i){pcm=block(*t);require(pcm==block(*reference),"Yamaha capture changes audio.");}
        require(rms(pcm)>10,"Yamaha fixture is silent.");aligned(*t,pcm);
        if(id==6&&!dual) {
            QVector<float> l,r;t->readTap(3,l,r);require(l==r,"YM2203 should be mono.");
            const auto mean=std::accumulate(l.begin(),l.end(),0.0)/l.size();int crossings=0;
            for(int i=1;i<l.size();++i)if(l[i-1]<=mean&&l[i]>mean)++crossings;
            const double actual=double(crossings)*SampleRate/ScopeFrames,expected=4000000.0/(2*16*80);
            require(std::abs(actual-expected)<expected*.05,"YM2203 SSG clock-derived pitch mismatch.");
        }
        if(id==3||id==12) {
            QVector<float> l,r;t->readTap(0,l,r);require(rms(l)>10&&rms(r)<1,"Yamaha left-only routing incorrect.");
            t->seek(60000);t->readTap(0,l,r);require(rms(r)>10&&rms(l)<1,"Yamaha right-only routing incorrect.");
            reference->seek(60000);
        }
        for(int c=0;c<t->info().voices.size();++c) {
            t->mute(~(1u<<c));pcm=block(*t);block(*reference);QVector<float> l,r;t->readTap(c,l,r);
            for(int i=0;i<1024;++i)require(pcm[2*i]==l[ScopeFrames-1024+i]&&pcm[2*i+1]==r[ScopeFrames-1024+i],"Yamaha mute/Solo chip mapping incorrect.");
        }
        t->mute(~0u);require(rms(block(*t))==0,"Yamaha mute-all is not silent.");block(*reference);
        t->mute(0);require(block(*t)==block(*reference),"Yamaha mutes changed emulation state.");
        t->seek(0);reference->seek(0);
        for(int n:{1,31,1024,7,4096,511,256})require(block(*t,n)==block(*reference,n),"Yamaha irregular chunk/rewind mismatch.");
        t->seek(5*SampleRate);reference->seek(5*SampleRate);pcm=block(*t);require(pcm==block(*reference),"Yamaha loop state mismatch.");aligned(*t,pcm);
        require(!t->seek(3600LL*SampleRate,[]{return true;}),"Yamaha cancellation ignored.");
        log<<"Yamaha "<<t->info().configuration.name<<": audio, chip-mix sync, stereo/mono, mute/Solo, rewind/chunks/loops PASS\n";log.flush();
    }
    // Timer-driven CSM must sound without an explicit manual key-on.
    auto csm=fixture(3);
    const QByteArray keyOn=QByteArray::fromHex("540878");const auto keyAt=csm.indexOf(keyOn,0x100);
    require(keyAt>=0,"CSM fixture key-on missing.");csm[keyAt+2]=0;
    write(path,csm);auto off=openMusicTrack(path);off->seek(SampleRate/2);require(rms(block(*off))<1,"CSM baseline is not silent.");
    const auto loopAt=0x1c+qFromLittleEndian<quint32>(csm.constData()+0x1c);
    csm.insert(loopAt,QByteArray::fromHex("5410f0541100541485"));qToLittleEndian<quint32>(csm.size()-4,csm.data()+4);
    qToLittleEndian<quint32>(loopAt+9-0x1c,csm.data()+0x1c);
    write(path,csm);auto timed=openMusicTrack(path);timed->seek(SampleRate/2);require(rms(block(*timed))>10,"Yamaha timer-driven CSM is silent.");
    // Different chip clocks, sources and identities in a single VGM stream.
    auto mixed=fixture(6);const auto opm=fixture(3);
    const auto initEnd=0x1c+qFromLittleEndian<quint32>(opm.constData()+0x1c);
    mixed.insert(0x100,opm.mid(0x100,initEnd-0x100));
    qToLittleEndian<quint32>(3579545,mixed.data()+0x30);qToLittleEndian<quint32>(mixed.size()-4,mixed.data()+4);
    qToLittleEndian<quint32>(qFromLittleEndian<quint32>(mixed.constData()+0x1c)+initEnd-0x100,mixed.data()+0x1c);
    write(path,mixed);auto combined=openMusicTrack(path,0,true,true);
    require(combined->info().configuration.sources==QStringList{"ym2151.mix:0","ym2203.0.fm:0","ym2203.0.fm:1","ym2203.0.fm:2",
        "ym2203.0.ssg:0","ym2203.0.ssg:1","ym2203.0.ssg:2"},"Mixed Yamaha preset identities unstable.");
    QElapsedTimer performance;performance.start();
    for(int i=0;i<1200;++i) {const auto p=block(*combined,2205);if(i%100==0)aligned(*combined,p);}
    log<<"Mixed Yamaha: 60 seconds with loops, two clock domains and capture in "<<performance.elapsed()<<" ms\n";
    QVector<float> l1,r1,l2,r2;combined->readTap(0,l1,r1);combined->readTap(4,l2,r2);
    require(rms(l1)>10&&rms(l2)>10&&l1!=l2,"Mixed Yamaha cards duplicate or miss a chip.");
    combined->seek(22050);auto mixedRef=openMusicTrack(path);mixedRef->seek(22050);require(block(*combined)==block(*mixedRef),"Mixed Yamaha rewind mismatch.");
    // VGM's dual-OPL2 flag means first chip left, second chip right.
    auto panned=fixture(9,true);qToLittleEndian<quint32>(0xc0000000u|3579545u,panned.data()+0x50);
    write(path,panned);auto hardPan=openMusicTrack(path,0,true,true);hardPan->seek(22050);
    hardPan->readTap(0,l1,r1);hardPan->readTap(1,l2,r2);
    require(hardPan->info().stereoOutput&&rms(l1)>10&&rms(r1)==0&&rms(l2)==0&&rms(r2)>10,"Dual-OPL2 VGM hard panning incorrect.");
    write(path,fixture(6,false,true,true));auto fm=openMusicTrack(path);fm->seek(SampleRate/2);require(rms(block(*fm))>10,"YM2203 FM is silent.");
    write(path,fixture(6,false,false));auto finite=openMusicTrack(path);finite->seek(3*SampleRate);require(rms(block(*finite))==0,"Yamaha played beyond stream end.");
    auto extra=fixture(6,true);qToLittleEndian<quint32>(0xc0-0xbc,extra.data()+0xbc);qToLittleEndian<quint32>(12,extra.data()+0xc0);
    qToLittleEndian<quint32>(8,extra.data()+0xc4);qToLittleEndian<quint32>(10,extra.data()+0xc8);
    extra[0xcc]=1;extra[0xcd]=6;qToLittleEndian<quint32>(3000000,extra.data()+0xce);
    extra[0xd2]=1;extra[0xd3]=char(0x86);extra[0xd4]=0;qToLittleEndian<quint16>(0x8180,extra.data()+0xd5);
    auto h=inspectVgm(extra);require(h.yamaha[1].clock==3000000&&h.yamaha[0].ssgGain==1.5,"Yamaha extra clocks/paired volumes ignored.");
    write(path,extra);auto ext=openMusicTrack(path,0,true,true);ext->seek(SampleRate);aligned(*ext,block(*ext));
    auto invalid=fixture(6);qToLittleEndian<quint32>(3579545,invalid.data()+12);reject(path,invalid,"combined");
    invalid=fixture(6);invalid[0x100]=char(0xa5);reject(path,invalid,"undeclared");
    invalid=fixture(6);invalid[0x100]=char(0x67);reject(path,invalid,"data-block marker");
    invalid=fixture(6);invalid.chop(1);qToLittleEndian<quint32>(invalid.size()-4,invalid.data()+4);reject(path,invalid,"no end");
    invalid=extra;qToLittleEndian<quint32>(0xffffffffu,invalid.data()+0xc8);reject(path,invalid,"volume header");
    invalid=fixture(6);invalid.insert(invalid.size()-1,QByteArray::fromHex("6462e803"));
    qToLittleEndian<quint32>(invalid.size()-4,invalid.data()+4);reject(path,invalid,"changing wait lengths");
    write(path,fixture(12,true));
    auto concurrent=[path]{auto t=openMusicTrack(path,0,true,true);t->seek(50000);return block(*t);};
    auto a=std::async(std::launch::async,concurrent),b=std::async(std::launch::async,concurrent);require(a.get()==b.get(),"Yamaha instances interfere.");
    const auto zipped=temp.filePath("test.vgz");const auto data=fixture(6);
    auto gz=gzopen(zipped.toUtf8().constData(),"wb");require(gz!=nullptr,"Cannot create Yamaha VGZ.");
    const int written=gzwrite(gz,data.constData(),unsigned(data.size()));const auto closed=gzclose(gz);require(written==data.size()&&closed==Z_OK,"Yamaha VGZ write failed.");
    auto compressed=openMusicTrack(zipped);require(rms(block(*compressed))>10,"Yamaha VGZ is silent.");
    log<<"Yamaha: FM/SSG, extra clock/volume, EOF, concurrency, gzip and invalid inputs PASS\n";log.flush();
    checkYamahaSamples(app,directory,log,decoderOnly);
    checkYamahaVoices(app,directory,log,decoderOnly);
    if(decoderOnly)return;
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    auto ready=[&]{const auto s=window.player().state();const auto f=window.player().scopes();return s.valid&&!s.busy&&s.sharedScopes&&f.generation==s.generation&&f.mask==3&&std::abs(s.positionMs-f.positionMs)<200;};
    until(app,ready,"Live Yamaha chip cards missing.");window.refresh();
    auto stereo=window.scopeWindow().findChild<QCheckBox*>("scopeStereo");require(stereo&&stereo->isEnabled(),"Yamaha stereo control unavailable.");stereo->setChecked(true);
    window.player().toggleSolo(1);until(app,[&]{return window.player().state().muteMask==~2u;},"Yamaha live Solo failed.");
    window.player().setOutputScopesEnabled(true);window.player().seek(500,true);
    until(app,[&]{return ready()&&window.player().outputScopes().sequence;},"Yamaha Full mix missing.");
    window.player().seek(750,false);until(app,ready,"Yamaha paused seek lost scopes.");
    window.player().setRepeat(true);window.player().seek(1900,true);const auto gen=window.player().state().generation;
    until(app,[&]{return ready()&&window.player().state().generation>gen;},"Yamaha repeat stalled.");
    window.player().setRepeat(false);window.player().seek(750,false);until(app,ready,"Yamaha pause failed.");
    window.refresh();window.scopeWindow().present(window.player().state(),window.player().scopes(),window.player().outputScopes());
    window.scopeWindow().grab().save(QDir(directory).filePath("native-yamaha.png"));
    window.scopeWindow().hide();until(app,[&]{return window.player().state().scopesSuspended;},"Hidden Yamaha scopes still active.");
    window.scopeWindow().show();until(app,ready,"Yamaha scopes failed to resume.");
    const auto s=window.player().state();require(!s.starvations&&!s.outputErrors,"Yamaha live audio errors.");window.close();window.player().shutdown();
    log<<"Yamaha live: chip cards, Solo, Full mix, seek/repeat, hide/resume and zero audio errors PASS\n";log.flush();
}

void checkYamahaFiles(QApplication &app,const QStringList &paths,QTextStream &log,bool decoderOnly) {
    for(const auto &path:paths) {
        QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read local Yamaha VGM.");const auto bytes=f.readAll();f.close();
        auto t=openMusicTrack(path,0,true,true);require(t->info().configuration.family=="yamaha-vgm","Local input is not Yamaha VGM.");
        QElapsedTimer timer;timer.start();double energy=0;for(int i=0;i<220;++i)energy+=rms(block(*t));require(energy>10,"Local Yamaha stream silent.");
        t->seek(30LL*SampleRate);aligned(*t,block(*t));t->seek(22050);auto ref=openMusicTrack(path);ref->seek(22050);require(block(*t)==block(*ref),"Local Yamaha rewind mismatch.");
        log<<"Local Yamaha: PCM/capture, 30-second seek and rewind PASS ("<<timer.elapsed()<<" ms)\n";log.flush();
        if(!decoderOnly) {PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
            until(app,[&]{const auto s=window.player().state();return s.valid&&!s.busy&&window.player().scopes().mask;},"Local Yamaha cards unavailable.");
            window.close();window.player().shutdown();}
        require(f.open(QIODevice::ReadOnly)&&f.readAll()==bytes,"Source music was modified.");
    }
}
