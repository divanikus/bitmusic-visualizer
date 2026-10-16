#include "ttttrack.h"
#include "tia-replay/replay.h"
#include "window.h"
#include <TIASnd.h>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <cmath>
#include <future>
#include <stdexcept>

namespace {
void require(bool v,const char *message) {if(!v) throw std::runtime_error(message);}
QByteArray pcm(MusicTrack &track,int count) {
    QByteArray b(count*4,0);track.render(reinterpret_cast<short*>(b.data()),count);return b;
}
QByteArray read(const QString &path) {QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read TTT fixture.");return f.readAll();}
float peak(const QVector<float> &v) {float p=0;for(float x:v)p=std::max(p,std::abs(x));return p;}
void alignment(MusicTrack &t) {
    const auto bytes=pcm(t,ScopeFrames);const auto *v=reinterpret_cast<const short*>(bytes.constData());
    QVector<float> a,b,r;
    require(t.readTap(0,a,r)&&a==r&&t.readTap(1,b,r)&&b==r,"TIA taps are not two mono voices.");
    require(peak(a)>20 && peak(b)>20 && a!=b,"TIA voices silent or duplicated.");
    for(int i=0;i<ScopeFrames;++i) require(v[2*i]==v[2*i+1] && std::abs(v[2*i]-std::clamp(a[i]+b[i],-32768.f,32767.f))<1,"TIA taps do not reconstruct audible mix.");
}
void spin(QApplication &app,const std::function<bool()> &ready,int limit=6000) {
    QElapsedTimer time;time.start();
    while(!ready() && time.elapsed()<limit) {app.processEvents();QThread::msleep(5);}
    require(ready(),"TIA live transport timed out.");
}
}
int runTttChecks(QApplication &app,const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--ttt-checks");require(at+1<args.size(),"Supply fixture directory.");
        QDir dir{QDir{args[at+1]}.absolutePath()};
        QTemporaryDir settings(dir.filePath("tia-settings-XXXXXX"));require(settings.isValid(),"Cannot isolate TIA settings.");
        app.setProperty("settingsFileForTests",settings.filePath("settings.ini"));
        app.setProperty("themesDirectoryForTests",settings.filePath("themes"));
        app.setProperty("presetsDirectoryForTests",settings.filePath("presets"));
        const auto path=dir.filePath("demo.ttt"); const auto bytes=read(path);const auto song=Ttt::compile(bytes);
        auto plain=openMusicTrack(path),tapped=openMusicTrack(path,0,true,true);
        require(tapped->info().voices.size()==2 && tapped->info().system=="Atari 2600" && !tapped->info().stereoOutput && tapped->info().title=="Bit Music TIA test","TIA metadata incorrect.");
        require(song.loop>=0 && tapped->info().durationMs>0 && tapped->info().playlist.size()==1,"TIA loop/duration missing.");
        // Arbitrary render chunks must give the same PCM, including tick boundaries.
        const auto all=pcm(*plain,12000);QByteArray chunks;
        for(int n:{1,137,1024,4096,6742}) chunks+=pcm(*tapped,n);
        require(all==chunks,"TIA rendering depends on chunk size or capture.");
        alignment(*tapped);
        const auto pos=tapped->frame();const auto expected=pcm(*tapped,4096);
        require(tapped->seek(pos) && pcm(*tapped,4096)==expected,"TIA rewind is not deterministic.");
        require(!tapped->seek(0,[]{return true;}) && tapped->frame()==pos+4096,"Cancelled TIA seek changed state.");
        auto concurrent=[path,pos] {auto t=openMusicTrack(path);t->seek(pos);return pcm(*t,4096);};
        auto future=std::async(std::launch::async,concurrent);require(concurrent()==expected && future.get()==expected,"TIA decoders share state.");
        for(int c=0;c<2;++c) {
            auto t=openMusicTrack(path,0,true,true);t->mute(VoiceMask::bit(1-c));t->seek(pos);
            const auto audio=pcm(*t,ScopeFrames);QVector<float> l,r;t->readTap(c,l,r);
            const auto *v=reinterpret_cast<const short*>(audio.constData());
            for(int i=0;i<ScopeFrames;++i) require(v[2*i]==l[i],"TIA solo leaked or altered a voice.");
            t->mute(VoiceMask::first(2));const auto silent=pcm(*t,1000);
            for(char x:silent)require(x==0,"TIA mute leaked.");
            t->mute({});auto ref=openMusicTrack(path);ref->seek(t->frame());
            require(pcm(*t,4096)==pcm(*ref,4096),"Muting stopped TIA oscillators or envelopes.");
        }
        // Native-rate separation must equal the unchanged upstream mono core.
        Emulation::TIASound mono(31400),split(31400);mono.channels(2,false);split.channels(2,true);
        for(const auto &tick:song.ticks) {
            for(int c=0;c<2;++c) for(auto *chip:{&mono,&split}) {
                chip->set(Emulation::AUDC0+c,tick.registers[c][0]);chip->set(Emulation::AUDF0+c,tick.registers[c][1]);chip->set(Emulation::AUDV0+c,tick.registers[c][2]);
            }
            short a[2],b[2];for(int i=0;i<31400/song.rate;++i) {mono.process(a,1);split.process(b,1);require(a[0]==a[1] && a[0]==b[0]+b[1],"Upstream TIA split differs from mono.");}
        }
        auto finite=openMusicTrack(dir.filePath("finite.ttt"));
        finite->seek(qint64(finite->info().durationMs+100)*SampleRate/1000);
        for(char x:pcm(*finite,4096))require(x==0,"Finite TTT continues after end.");
        auto ntsc=openMusicTrack(dir.filePath("ntsc.ttt"),0,true,true);alignment(*ntsc);
        require(ntsc->info().durationMs<tapped->info().durationMs,"NTSC speed was ignored.");
        const auto local=Ttt::compile(read(dir.filePath("local-speed.ttt")));
        require(local.ticks[4].registers!=song.ticks[4].registers || local.ticks.size()!=song.ticks.size(),"Per-pattern speed was ignored.");
        // Corrupt project data must fail explicitly, never clamp/repair it.
        auto rejects=[&](QJsonObject object){bool threw=false;try{Ttt::compile(QJsonDocument(object).toJson());}catch(const std::exception&){threw=true;}require(threw,"Malformed TTT accepted.");};
        auto root=QJsonDocument::fromJson(bytes).object(),bad=root;
        bad["evenspeed"]=0;rejects(bad);bad=root;bad["version"]=2;rejects(bad);
        bad=root;bad["channels"]=QJsonArray{};rejects(bad);
        bad=root;auto channels=bad["channels"].toArray();auto channel=channels[0].toObject();auto seq=channel["sequence"].toArray();auto entry=seq[0].toObject();entry["patternindex"]=9999;seq[0]=entry;channel["sequence"]=seq;channels[0]=channel;bad["channels"]=channels;rejects(bad);
        require(!Ttt::recognizes("{}") && !Ttt::recognizes("not music"),"Unrelated input recognized as TTT.");
        log<<"TIA decoder PASS: PAL/NTSC, metadata, duration, loop/end, upstream mono/split equality, chunk-independent PCM, aligned voices, mute/Solo, rewind/cancellation, concurrent decoders and malformed inputs.\n";log.flush();
        auto extra=args.mid(at+2);extra.removeAll("--decoder-only");extra.removeAll("--software-scopes");
        int total=0;QElapsedTimer timer;timer.start();
        for(const auto &file:extra) {
            auto t=openMusicTrack(file,0,true,true);auto ref=openMusicTrack(file);
            for(int i=0;i<20;++i) require(pcm(*t,1024)==pcm(*ref,1024),"Local TTT capture changed output.");
            t->seek(30*SampleRate);ref->seek(30*SampleRate);require(pcm(*t,4096)==pcm(*ref,4096),"Local TTT transport drifted.");
            t->seek(0);auto fresh=openMusicTrack(file);require(pcm(*t,4096)==pcm(*fresh,4096),"Local TTT rewind mismatch.");++total;
        }
        if(total)log<<"Local TTT projects PASS: "<<total<<"; decode/seek checks "<<timer.elapsed()<<" ms.\n";
        if(!args.contains("--decoder-only")) {
            PlayerWindow window;window.player().setVolume(0);window.show();window.scopeWindow().show();window.loadFile(path);
            auto &p=window.player();p.setScopesEnabled(true);p.setScopeMask(VoiceMask::first(2));p.setOutputScopesEnabled(true);
            spin(app,[&]{return p.state().playing && p.scopes().mask==VoiceMask::first(2);});
            p.toggleSolo(0);require(p.state().soloChannel==0,"TIA live Solo failed.");p.toggleSolo(0);
            p.seek(500,false);spin(app,[&]{return !p.state().busy && !p.state().playing && p.scopes().generation==p.state().generation && p.scopes().mask==VoiceMask::first(2);});
            const auto duration=p.state().info.durationMs;p.setRepeat(true);p.seek(duration-100,true);
            spin(app,[&]{return !p.state().busy && p.state().positionMs>=duration-100;});
            spin(app,[&]{return !p.state().busy && p.state().positionMs<duration-200 && p.scopes().mask==VoiceMask::first(2);});
            p.setRepeat(false);p.seek(duration-100,true);spin(app,[&]{return !p.state().busy && !p.state().playing && p.state().positionMs>=duration;});
            p.setRepeat(true);p.seek(0,true);
            spin(app,[&]{const auto s=p.state();return !s.busy && s.playing && s.positionMs>=300 && p.scopes().generation==s.generation && p.scopes().mask==VoiceMask::first(2);});
            p.setOutputScopesEnabled(true);
            spin(app,[&]{return p.outputScopes().left.size()==ScopeFrames;});
            window.scopeWindow().present(p.state(),p.scopes(),p.outputScopes());
            app.processEvents();
            window.scopeWindow().grab().save(dir.filePath("tia-scopes.png"));window.close();
            log<<"TIA live PASS: cards, paused seek, repeat, end/restart, Solo and output capture.\n";
        }
        return 0;
    } catch(const std::exception &e) {log<<"FAIL: "<<e.what()<<'\n';return 1;}
}
