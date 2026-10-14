#include "window.h"
#include "vgm.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &data) {
    QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(data) == data.size(), "Cannot write POKEY fixture.");
}
QByteArray fixture(int mode = 0, bool loop = true) {
    // Original register patterns, not game code/music. Four different pitches.
    QByteArray data(0xc0, '\0'); data.replace(0, 4, "Vgm ");
    auto word = [&](int offset, quint32 value) { qToLittleEndian(value, data.data()+offset); };
    word(8, 0x161); word(0xb0,1789790); word(0x34,0xc0-0x34);
    auto reg = [&](int address,int value) { data += char(0xbb); data += char(address); data += char(value); };
    for (int i = 0; i < 4; ++i) { reg(i*2, 50+30*i); reg(i*2+1, mode == 1 ? 0x8b : 0xab); }
    if (mode == 2) { reg(1,0); reg(0,100); reg(2,3); reg(8,0x50); } // Joined high-clock pair.
    if (mode == 3) reg(8,4); // Channel 3 clocks channel 1's high-pass gate.
    const int loopAt = data.size();
    for (int i = 0; i < 120; ++i) {
        if (mode == 4) reg(1,0x10 | (i & 15)); // Volume-only DAC.
        if (i == 60) reg(6,40);
        data += char(0x62);
    }
    data += char(0x66);
    word(4,quint32(data.size()-4)); word(0x18,88200);
    if (loop) { word(0x1c,loopAt-0x1c); word(0x20,88200); }
    return data;
}
QVector<short> block(MusicTrack &t, int n=1024) { QVector<short> p(n*2); t.render(p.data(),n); return p; }
template<class T> double rms(const T &p) { double e=0; for (auto x:p) e+=double(x)*x; return std::sqrt(e/std::max<qsizetype>(1,p.size())); }
QVector<QVector<float>> traces(MusicTrack &t) {
    QVector<QVector<float>> result(4); QVector<float> right;
    for (int c=0;c<4;++c) require(t.readTap(c,result[c],right) && result[c]==right,"POKEY mono tap missing or mismatched.");
    return result;
}
void rejected(const QString &path, const QByteArray &data, const QString &message) {
    write(path,data); QString error;
    try { auto t=openMusicTrack(path); } catch(const std::exception &e) { error=QString::fromUtf8(e.what()); }
    require(error.contains(message),"VGM unsupported/invalid input was not rejected with the expected explanation.");
}
void until(QApplication &app,const std::function<bool()> &ready,const char *message) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed()<12000) {app.processEvents();QThread::msleep(4);}
    require(ready(),message);
}
}

void checkPokey(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    QTemporaryDir temp(QDir(directory).filePath("pokey-XXXXXX")); require(temp.isValid(),"No POKEY test directory.");
    const auto path=temp.filePath("test.vgm");
    for (int mode=0;mode<5;++mode) {
        write(path,fixture(mode));
        auto master=openMusicTrack(path), capture=openMusicTrack(path,0,true,true);
        require(capture->hasTaps() && capture->info().voices.size()==4 && !capture->info().stereoOutput &&
            capture->info().configuration.family=="pokey" && capture->info().configuration.sources[0]=="pokey.voice:0",
            "POKEY capabilities/configuration mismatch.");
        QVector<short> pcm;
        for(int i=0;i<100;++i) { pcm=block(*capture); require(pcm==block(*master),"POKEY capture changed master PCM."); }
        require(rms(pcm)>10,"POKEY master silent.");
        auto views=traces(*capture);
        if (!mode) {
            for (const auto &v:views) require(rms(v)>10,"POKEY voice silent.");
            for (int c=1;c<4;++c) require(views[c]!=views[0],"POKEY voices duplicate one another.");
            // Independent check against the chip's 28-clock divider, not another decoder instance.
            int crossings=0;
            for(int i=1;i<ScopeFrames;++i) if(views[0][i-1]<=0 && views[0][i]>0) ++crossings;
            const double measured=double(crossings)*SampleRate/ScopeFrames;
            const double expected=1789790.0/(28*51*2);
            require(std::abs(measured-expected)<expected*.04,"POKEY pitch/timing differs from register clock.");
        }
        for(int i=0;i<1024;++i) {
            int sum=0;for(int c=0;c<4;++c)sum+=int(views[c][ScopeFrames-1024+i]);
            require(pcm[2*i]==std::clamp(sum,-32768,32767) && pcm[2*i]==pcm[2*i+1],"POKEY taps are out of sync with audible PCM.");
        }
        for (int c=0;c<4;++c) {
            capture->mute(15u ^ (1u<<c)); const auto solo=block(*capture); views=traces(*capture);
            for(int i=0;i<1024;++i) require(solo[2*i]==int(views[c][ScopeFrames-1024+i]),"POKEY Solo/mute mapping incorrect.");
            block(*master);
        }
        capture->mute(15); require(rms(block(*capture))==0,"POKEY all-muted output is not silent.");block(*master);
        capture->mute(0);require(block(*capture)==block(*master),"Muting changed POKEY oscillator/high-pass state.");
        capture->seek(0); master->seek(0);
        for(int n:{1,7,31,256,17,2048,4096,15555})require(block(*capture,n)==block(*master,n),"POKEY rewind/chunking mismatch.");
        auto reference=openMusicTrack(path,0,true,true); reference->seek(capture->frame());
        require(traces(*reference)==traces(*capture),"POKEY irregular chunks changed tap history.");
        require(!capture->seek(3600LL*SampleRate,[]{return true;}),"POKEY seek ignored cancellation.");
    }
    write(path,fixture());
    auto continuous=openMusicTrack(path,0,true,true);
    for(int i=0;i<2400;++i) {block(*continuous,2205);traces(*continuous);} // Two minutes, many VGM loops.
    continuous->seek(22050); auto reference=openMusicTrack(path,0,true,true);reference->seek(22050);
    require(traces(*continuous)==traces(*reference),"POKEY long playback/rewind history mismatch.");
    auto concurrent=[path]{ auto t=openMusicTrack(path,0,true,true);t->seek(100000);return block(*t);};
    auto a=std::async(std::launch::async,concurrent),b=std::async(std::launch::async,concurrent);
    require(a.get()==b.get(),"POKEY instances interfere with one another.");
    write(path,fixture(0,false)); auto finite=openMusicTrack(path);finite->seek(3*SampleRate);
    require(rms(block(*finite))<1,"POKEY continued sounding after end of stream.");
    const auto compressed=temp.filePath("test.vgz"); const auto bytes=fixture();
    auto gz=gzopen(compressed.toUtf8().constData(),"wb");require(gz!=nullptr,"Cannot create compressed fixture.");
    const auto written=gzwrite(gz,bytes.constData(),unsigned(bytes.size()));const auto closed=gzclose(gz);
    require(written==bytes.size() && closed==Z_OK,"VGZ compression failed.");
    auto zipped=openMusicTrack(compressed);require(rms(block(*zipped))>10,"POKEY VGZ failed.");
    for(const auto &chip: {std::pair<int,const char*>{0x60,"YMF278B"},{0x84,"NES APU"},{0xc0,"WonderSwan"},{0xb8,"SCSP"}}) {
        QByteArray data(0x100,'\0');data.replace(0,4,"Vgm ");qToLittleEndian<quint32>(0x171,data.data()+8);
        qToLittleEndian<quint32>(0x100-0x34,data.data()+0x34);qToLittleEndian<quint32>(1000000,data.data()+chip.first);
        data+=char(0x66);qToLittleEndian<quint32>(data.size()-4,data.data()+4);rejected(path,data,chip.second);
    }
    auto extra=fixture();qToLittleEndian<quint32>(0x170,extra.data()+8);
    extra.insert(0xc0,QByteArray(16,'\x7f'));qToLittleEndian<quint32>(4,extra.data()+0xbc);
    qToLittleEndian<quint32>(0xd0-0x34,extra.data()+0x34);
    require(inspectVgm(extra).unsupported.isEmpty(),"VGM extra header was interpreted as chip clocks.");
    auto invalid=fixture();qToLittleEndian<quint32>(0x401b4f5e,invalid.data()+0xb0);rejected(path,invalid,"dual/variant POKEY");
    invalid=fixture();qToLittleEndian<quint32>(3579545,invalid.data()+12);rejected(path,invalid,"combined");
    invalid=fixture();invalid[0xc1]=15;invalid[0xc2]=0;rejected(path,invalid,"SKCTL");
    invalid=fixture();invalid.chop(2);qToLittleEndian<quint32>(invalid.size()-4,invalid.data()+4);rejected(path,invalid,"no end");
    invalid=fixture();qToLittleEndian<quint32>(0xffffffffu,invalid.data()+0x34);rejected(path,invalid,"data offset");
    log<<"POKEY: four signals, exact PCM/tap sync, mono, noise/DAC/join/high-pass, mute/Solo, seeks, loops, two-minute history, chunking, concurrent instances, gzip and unsupported-chip errors PASS\n";log.flush();
    if(decoderOnly)return;
    write(path,fixture());
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    auto ready=[&]{const auto s=window.player().state();const auto f=window.player().scopes();
        return s.valid&&!s.busy&&s.sharedScopes&&f.generation==s.generation&&f.mask==15&&std::abs(s.positionMs-f.positionMs)<200;};
    until(app,ready,"POKEY live scopes unavailable.");window.refresh();
    auto &scope=window.scopeWindow();auto stereo=scope.findChild<QCheckBox*>("scopeStereo");
    require(stereo&&!stereo->isEnabled(),"POKEY mono stream offers stereo.");
    window.player().toggleSolo(2);until(app,[&]{return window.player().state().muteMask==~4u;},"POKEY live Solo failed.");
    window.player().seek(750,false);until(app,ready,"POKEY paused seek lost scopes.");
    window.player().seek(0,true);until(app,ready,"POKEY restart lost scopes.");
    window.player().setRepeat(true);window.player().seek(1900,true);
    const auto repeatGeneration=window.player().state().generation;
    until(app,[&]{return ready() && window.player().state().generation>repeatGeneration;},"POKEY transport repeat stalled.");
    window.player().setRepeat(false);window.player().setOutputScopesEnabled(true);
    window.player().seek(500,true);
    until(app,[&]{return ready() && window.player().state().positionMs>=750 && window.player().outputScopes().sequence;},"POKEY Full mix history missing.");
    window.player().seek(750,false);until(app,ready,"POKEY final pause lost scopes.");
    window.refresh();scope.present(window.player().state(),window.player().scopes(),window.player().outputScopes());
    scope.grab().save(QDir(directory).filePath("native-pokey.png"));
    scope.hide();until(app,[&]{return window.player().state().scopesSuspended;},"Hidden POKEY scopes still compute.");
    scope.show();until(app,ready,"POKEY scopes did not resume.");
    const auto state=window.player().state();require(!state.starvations&&!state.outputErrors,"POKEY live audio errors.");
    const auto badPath=temp.filePath("unsupported.vgm");auto bad=fixture();
    qToLittleEndian<quint32>(0,bad.data()+0xb0);qToLittleEndian<quint32>(4000000,bad.data()+0x60);write(badPath,bad);
    window.loadFile(badPath);
    until(app,[&]{return window.player().state().error.contains("YMF278B");},"Unsupported VGM error did not reach the UI.");
    require(!window.player().state().playing,"Unsupported VGM kept playing.");
    window.loadFile(path);until(app,ready,"POKEY did not recover after unsupported VGM.");
    window.close();window.player().shutdown();
    log<<"POKEY live: shared four-card scopes, mono UI, Solo, paused seek/restart, hide/resume, unsupported-chip error/recovery, zero audio errors PASS\n";log.flush();
}

void checkPokeyFiles(QApplication &app,const QStringList &paths,QTextStream &log,bool decoderOnly) {
    for(const auto &path:paths) {
        QFile f(path);require(f.open(QIODevice::ReadOnly),"Cannot read local VGM.");const auto original=f.readAll();f.close();
        auto t=openMusicTrack(path,0,true,true);
        if(t->info().configuration.family!="pokey")continue;
        QElapsedTimer timer;timer.start();double energy=0;
        for(int i=0;i<220;++i)energy+=rms(block(*t));
        require(energy>10,"Local POKEY stream silent.");traces(*t);
        t->seek(30LL*SampleRate);traces(*t);t->seek(22050);traces(*t);
        auto reference=openMusicTrack(path,0,true,true);reference->seek(22050);
        require(traces(*t)==traces(*reference),"Local POKEY rewind changes traces.");
        log<<"Local POKEY: audio + four taps, 30-second/rewind seeks PASS ("<<timer.elapsed()<<" ms)\n";log.flush();
        if(!decoderOnly) {
            PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
            until(app,[&]{const auto s=window.player().state();return s.valid&&!s.busy&&window.player().scopes().mask==15;},"Local POKEY scopes unavailable.");
            window.refresh();
            window.close();window.player().shutdown();
        }
        require(f.open(QIODevice::ReadOnly)&&f.readAll()==original,"Local music was modified.");
    }
}
