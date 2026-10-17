#include "sidtrack.h"
#include "romfiles.h"
#include <QTemporaryDir>
#include <QMessageBox>
#include "playerwindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QTextStream>
#include <QCheckBox>
#include <QListWidget>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <future>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size())
        throw std::runtime_error(("Cannot write SID fixture: " + f.errorString() + " (" + path + ")").toStdString());
}
QByteArray fixture(bool newer, bool ntsc, bool modulation = false, int active = 7, bool filter = false, int chips = 1) {
    // Original 6502 init/play routines; no game code, ROM or soundtrack.
    QByteArray code = QByteArray::fromHex("8502"); // Save zero-based subsong.
    int chipOffset = 0;
    auto reg = [&](int address, int value) { code += char(0xa9); code += char(value); code += char(0x8d); code += char(address+chipOffset); code += char(0xd4); };
    for (int chip = 0; chip < chips; ++chip) {
        chipOffset = 0x20*chip;
        reg(0x18, filter ? 0x1f : 15); reg(0x15, 0); reg(0x16, 50); reg(0x17, filter ? 7 : 0);
        for (int i = 0; i < 3; ++i) {
            reg(i*7, 0);
            code += char(0xa9); code += char(16+8*i+2*chip); code += QByteArray::fromHex("1865028d"); code += char(i*7+1+chipOffset); code += char(0xd4);
            reg(i*7+2, 0); reg(i*7+3, 8); reg(i*7+5, 0); reg(i*7+6, 0xf0);
            reg(i*7+4, (active & (1 << i)) ? (i == 0 ? (modulation ? 0x17 : 0x11) : i == 1 ? 0x21 : 0x41) : 0);
        }
    }
    code += char(0x60);
    const int play = 0x1000+code.size(); code += char(0x60);
    QByteArray data(0x7c, '\0'); data.replace(0,4,"PSID");
    auto word=[&](int at,int value){qToBigEndian<quint16>(value,data.data()+at);};
    word(4,chips == 1 ? 2 : chips+1); word(6,0x7c); word(8,0x1000); word(10,0x1000); word(12,play); word(14,2); word(16,1);
    word(0x76,(newer ? 0x20 : 0x10) | (ntsc ? 8 : 4) | (chips > 1 ? (newer ? 0x40 : 0x80) : 0) | (chips > 2 ? (newer ? 0x200 : 0x100) : 0));
    if (chips > 1) data[0x7a] = 0x42;
    if (chips > 2) data[0x7b] = 0x44;
    data.replace(0x16,16,"SID fixture test"); data+=code; return data;
}
QVector<short> block(MusicTrack &track, int frames = 1024) {
    QVector<short> pcm(frames*2); track.render(pcm.data(),frames); return pcm;
}
template<class T> double rms(const T &samples) { double s=0; for (auto v:samples) s+=double(v)*v; return std::sqrt(s/std::max<qsizetype>(1,samples.size())); }
QVector<QVector<float>> taps(const MusicTrack &track) {
    QVector<QVector<float>> result(track.info().voices.size()); QVector<float> right;
    for (int i=0;i<result.size();++i) {
        require(track.readTap(i,result[i],right),"SID tap history missing.");
        if (!track.info().stereoOutput) require(result[i]==right,"Single SID must be mono.");
    }
    return result;
}
void until(QApplication &app,const std::function<bool()> &fn,const char *message) {
    QElapsedTimer t; t.start(); while (!fn() && t.elapsed()<15000) {app.processEvents();QThread::msleep(4);} require(fn(),message);
}
QByteArray rsidFixture(int chips = 1) {
    auto data = fixture(true, false, false, 7, false, chips);
    auto code = data.mid(0x7c); code.chop(1); // Discard the PSID play routine.
    const int irq = 0x1000+code.size()+10;
    QByteArray setup = QByteArray::fromHex("a9008d1403a9008d1503");
    setup[1] = char(irq); setup[6] = char(irq >> 8);
    code.insert(code.size()-1, setup);
    code += QByteArray::fromHex("ee01d4ad0ddc4c81ea"); // Own CIA IRQ; use minimal IRQ return.
    data.truncate(0x7c); data.replace(0,4,"RSID");
    qToBigEndian<quint16>(0,data.data()+8); qToBigEndian<quint16>(0,data.data()+12);
    data += QByteArray::fromHex("0010"); data += code; return data;
}
QByteArray romProbe(int index) {
    auto data = fixture(true, false);
    data.chop(1);
    data += QByteArray::fromHex(index == 0 ? "2000e060" : index == 1 ? "ad00b060" : "a9338501ad00d0a937850160");
    return data;
}
QByteArray testKernal() {
    // Original test-only ROM: reset halt, an IRQ entry and one diagnostic function.
    QByteArray rom(8192, char(0xea));
    rom.replace(0,6,QByteArray::fromHex("a9308d01d460"));
    rom[0x1ce2] = 2;
    rom.replace(0x1f48,8,QByteArray::fromHex("488a4898486c1403"));
    rom.replace(0x1ffa,6,QByteArray::fromHex("e2fce2fc48ff"));
    return rom;
}
void extendedSid(const QString &path, const QString &romRoot, QTextStream &log) {
    for (int chips : {2,3}) for (bool rsid : {false,true}) {
        write(path, rsid ? rsidFixture(chips) : fixture(true,false,false,7,false,chips));
        auto master=openMusicTrack(path), captured=openMusicTrack(path,0,true,true);
        require(master->info().voices.size()==chips*3 && master->info().stereoOutput,"Multi-SID metadata incorrect.");
        require(master->info().configuration.sources[0]=="sid.voice:0" && master->info().configuration.sources[3]=="sid.d420.voice:0", "SID preset source identity changed.");
        for(int i=0;i<60;++i) require(block(*master)==block(*captured),"Multi-SID capture changes master PCM.");
        auto traces=taps(*captured);
        QVector<float> l,r;
        for(int ch=0;ch<chips*3;++ch) {
            require(rms(traces[ch])>100,"Multi-SID voice silent.");
            for(int prev=0;prev<ch;++prev) require(traces[ch]!=traces[prev],"Multi-SID voice duplicated.");
            if (!rsid) {
                double mean=0;for(float v:traces[ch]) mean+=v;mean/=traces[ch].size();
                int crossings=0,last=-100;
                for(int i=1;i<traces[ch].size();++i) if(traces[ch][i-1]<mean && traces[ch][i]>=mean && i-last>30) {++crossings;last=i;}
                const double hz=double(crossings)*SampleRate/traces[ch].size();
                const double expected=985248.61*(16+8*(ch%3)+2*(ch/3))*256/16777216.;
                require(std::abs(hz-expected)<15,"Multi-SID chip/voice order incorrect.");
            }
            captured->readTap(ch,l,r);
            const double ratio=rms(l)/rms(r);
            require(std::abs(ratio-(ch/3==0 ? 2. : ch/3==chips-1 ? .5 : 1.))<.001,"Multi-SID pan disagrees with mixer.");
        }
        captured->seek(SampleRate); traces=taps(*captured);
        captured->seek(2*SampleRate); captured->seek(SampleRate-24);
        for(int n:{1,7,16}) block(*captured,n);
        require(taps(*captured)==traces,"Multi-SID rewind changed traces.");
        const unsigned mask=(1u<<(chips*3))-1;
        for(int ch=0;ch<chips*3;++ch) {
            master->mute(mask ^ (1u<<ch)); master->seek(0); master->seek(SampleRate);
            require(rms(block(*master))>40,"Multi-SID Solo silenced selected voice.");
        }
        master->mute(mask); master->seek(2*SampleRate);
        const auto muted=block(*master);
        // Mixed chip models can retain a small DC offset; no voice oscillation.
        for(int side=0;side<2;++side) {
            QVector<float> ac; double mean=0;
            for(int i=side;i<muted.size();i+=2) {ac << muted[i];mean+=muted[i];}
            mean/=ac.size(); for(auto &v:ac) v-=float(mean);
            require(rms(ac)<5 && std::abs(mean)<100,"Multi-SID all-mute failed.");
        }
        auto concurrent=[&]{auto t=openMusicTrack(path,0,true,true);t->seek(SampleRate);return std::make_pair(block(*t),taps(*t));};
        auto a=std::async(std::launch::async,concurrent),b=std::async(std::launch::async,concurrent);
        require(a.get()==b.get(),"Concurrent multi-SID instances diverged.");
        log << (rsid ? "RSID" : "PSID") << " " << chips << " chips: distinct voices, pan, PCM equality, seeks, Solo and concurrency PASS\n";log.flush();
    }
    const std::array<const char *,3> names{"kernal.rom","basic.rom","chargen.rom"};
    auto expectRom=[&](int index){
        try {auto t=openMusicTrack(path);t->seek(SampleRate);}
        catch(const std::exception &e) {
            const auto message=QString::fromUtf8(e.what());
            require(message.startsWith("C64 ROM required:") && message.contains(names[index]) && message.contains("Then reopen"),"Missing ROM warning incorrect.");return;
        }
        throw std::runtime_error("Required ROM was not detected.");
    };
    for(int i=0;i<3;++i) {write(path,romProbe(i));expectRom(i);}
    auto nop=fixture(true,false);nop.chop(1);nop+=QByteArray::fromHex("0c00e01c00a060");
    write(path,nop);auto unused=openMusicTrack(path);unused->seek(SampleRate); // Discarded NOP reads do not need a ROM.
    auto loadY=fixture(true,false);loadY.chop(1);loadY+=QByteArray::fromHex("a200bc00a060");
    write(path,loadY);expectRom(1); // LDY abs,X shares low opcode bits with NOP abs,X.
    auto basic=rsidFixture(); qToBigEndian<quint16>(0,basic.data()+10); basic[0x77]=char(0x26);
    write(path,basic);expectRom(1); // Header explicitly requests BASIC execution.
    const auto kernalPath=QDir(romRoot).filePath("c64/kernal.rom");
    write(kernalPath,QByteArray("bad"));
    write(path,fixture(true,false));auto ordinary=openMusicTrack(path);ordinary->seek(SampleRate);
    write(path,rsidFixture());auto selfContained=openMusicTrack(path);selfContained->seek(SampleRate);
    require(rms(block(*selfContained))>100,"Self-contained RSID should run without ROMs.");
    write(path,romProbe(0));expectRom(0);
    write(kernalPath,testKernal());
    auto loaded=openMusicTrack(path,0,true,true);loaded->seek(SampleRate);
    require(rms(block(*loaded))>100,"Supplied test ROM was not executed.");
    // Deleting or replacing a file cannot desynchronize an already loaded track.
    require(QFile::remove(kernalPath),"Cannot remove temporary ROM.");
    auto scope=openMusicTrack(path,0,true,true,loaded->info().sidRoms);
    loaded->seek(0);loaded->seek(SampleRate);scope->seek(SampleRate);
    require(block(*loaded)==block(*scope) && taps(*loaded)==taps(*scope),"ROM snapshot changed after file removal.");
    expectRom(0); // Reopening takes a fresh snapshot.
    log << "ROMs: lazy requirements, no-ROM PSID/RSID, malformed optional file, supplied code, shared snapshot and reopen PASS\n";log.flush();
}
}

void checkSid(QApplication &app,const QString &directory,QTextStream &log,bool decoderOnly) {
    QTemporaryDir romRoot(QDir(directory).filePath("roms-XXXXXX")); require(romRoot.isValid(), "Cannot isolate ROM tests.");
    const auto oldRoms = app.property("romsDirectoryForTests");
    app.setProperty("romsDirectoryForTests", romRoot.path());
    struct Restore { QApplication &app; QVariant value; ~Restore() { app.setProperty("romsDirectoryForTests", value); } } restore{app,oldRoms};
    require(QDir().mkpath(romRoot.filePath("c64")), "Cannot create isolated C64 ROM directory.");
    const auto path=QDir(directory).filePath("native-test.sid");
    for (bool newer:{false,true}) for (bool ntsc:{false,true}) {
        write(path,fixture(newer,ntsc));
        auto master=openMusicTrack(path); auto captured=openMusicTrack(path,0,true,true);
        require(master->info().songs==2 && master->info().playlist.size()==2 && master->info().voices.size()==3 && !master->info().stereoOutput && master->info().durationMs==-1,"SID metadata incorrect.");
        require(master->info().configuration.name==(newer ? "SID 8580" : "SID 6581"),"SID model incorrect.");
        for(int i=0;i<50;++i) require(block(*master)==block(*captured),"SID capture changes master PCM.");
        auto traces=taps(*captured);
        for(auto &v:traces) require(rms(v)>200,"SID voice is silent.");
        require(traces[0]!=traces[1] && traces[1]!=traces[2] && traces[0]!=traces[2],"SID taps duplicated.");
        // Expected PAL/NTSC oscillator rates, measured from the actual traces.
        for(int ch=0;ch<3;++ch) {
            auto &v=traces[ch]; double mean=0; for(float x:v) mean+=x; mean/=v.size();
            // 6581 DAC nonlinearity can cross zero twice within a few samples.
            // Debounce those edges; all fixture fundamentals are below 600 Hz.
            int crossings=0,last=-100;
            for(int i=1;i<v.size();++i) if(v[i-1]<mean && v[i]>=mean && i-last>30) {++crossings;last=i;}
            const double hz=double(crossings)*SampleRate/v.size();
            const double expected=(ntsc ? 1022727.14 : 985248.61)*(16+8*ch)*256/16777216.0;
            require(std::abs(hz-expected)<15,"SID independent voice pitch mismatch.");
        }
        captured->seek(SampleRate); traces=taps(*captured);
        captured->seek(2*SampleRate); captured->seek(SampleRate-24);
        for(int n:{1,7,16}) block(*captured,n);
        require(taps(*captured)==traces,"SID rewind or irregular blocks changed voice traces.");
        int cancellations=0;
        require(!captured->seek(3600LL*SampleRate,[&]{return ++cancellations>3;}) && cancellations==4,"SID seek is not cancellable.");
        auto second=openMusicTrack(path,1,false,true); second->seek(SampleRate);
        require(taps(*second)!=traces,"SID subsong did not change pitch.");
        master->mute(7); master->seek(3*SampleRate);
        require(rms(block(*master))<10,"SID all-mute failed.");
        master->mute(0); for(int i=0;i<12;++i) block(*master);
        require(rms(block(*master))>100,"SID unmute failed.");
        log << "SID " << (newer?8580:6581) << (ntsc?" NTSC":" PAL") << ": PCM equality, pitch/isolation, subsongs, rewind, mute and cancel PASS\n";log.flush();
    }
    // Muting a ring/sync source must not change the other oscillator's trace.
    write(path,fixture(true,false,true));
    auto all=openMusicTrack(path,0,true,true),solo=openMusicTrack(path,0,true,true);solo->mute(6);
    all->seek(SampleRate);solo->seek(SampleRate);
    require(taps(*all)==taps(*solo),"SID mute alters oscillator modulation.");
    require(block(*all)!=block(*solo),"SID mute did not affect sound.");
    // Shared filter changes the mix, not the dry voice capture.
    write(path,fixture(true,false,false,7,true)); auto filtered=openMusicTrack(path,0,true,true);filtered->seek(SampleRate);
    write(path,fixture(true,false));auto dry=openMusicTrack(path,0,true,true);dry->seek(SampleRate);
    require(taps(*filtered)==taps(*dry) && block(*filtered)!=block(*dry),"SID filter/tap boundary incorrect.");
    // A disabled oscillator must not inherit another voice's PCM.
    write(path,fixture(true,false,false,1));auto one=openMusicTrack(path,0,true,true);one->seek(SampleRate);
    const auto isolated=taps(*one);
    auto span=[](const QVector<float> &v){auto p=std::minmax_element(v.begin(),v.end());return *p.second-*p.first;};
    // A silent DAC retains a small DC leakage offset, not an audible waveform.
    require(rms(isolated[0])>200 && span(isolated[1])<1 && span(isolated[2])<1,"Silent SID voices contain other voices.");
    auto rejects=[&](QByteArray data,const char *text){write(path,data);try {auto t=openMusicTrack(path);}catch(const std::exception &e){require(QString::fromUtf8(e.what()).contains(text),"SID rejection reason incorrect.");return;}throw std::runtime_error("Unsupported SID accepted.");};
    auto data=fixture(false,false); data.replace(0,4,"RSID");rejects(data,"invalid");
    rejects(QByteArray("PSID"),"truncated");
    extendedSid(path,romRoot.path(),log);
    write(path,fixture(false,false));
    auto decode=[&] {auto t=openMusicTrack(path,0,true,true);t->seek(SampleRate);return std::make_pair(block(*t),taps(*t));};
    auto a=std::async(std::launch::async,decode),b=std::async(std::launch::async,decode);
    require(a.get()==b.get(),"Concurrent SID instances diverged.");
    QElapsedTimer bench;bench.start();auto perf=openMusicTrack(path,0,false,true);perf->seek(30LL*SampleRate);
    log << "SID: 30 seconds with all voice taps decoded in " << bench.elapsed() << " ms\n";log.flush();
    log << "SID: ring/sync-preserving mute, shared filter, silent voices, unsupported inputs and concurrent instances PASS\n"; log.flush();
    if (decoderOnly) return;
    PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
    auto &player=window.player();
    auto ready=[&] {auto s=player.state();auto f=player.scopes();return s.valid && !s.busy && s.error.isEmpty() && s.scopeError.isEmpty() && f.generation==s.generation && f.mask==7 && std::abs(s.positionMs-f.positionMs)<150;};
    until(app,ready,"SID live scopes did not prepare.");window.refresh();
    require(player.state().sharedScopes,"SID scopes use multiple emulations.");
    require(window.findChild<QListWidget *>("songs")->count()==2,"SID UI playlist incomplete.");
    require(!window.scopeWindow().findChild<QCheckBox *>("scopeStereo")->isEnabled(),"SID stereo option not disabled.");
    player.setMuteMask(4);player.toggleSolo(1);require(player.state().soloChannel==1 && player.state().muteMask==~VoiceMask::bit(1),"SID Solo failed.");
    for(int ms:{5000,250,1000}) {
        player.seek(ms,false);until(app,ready,"SID seek stuck Preparing.");
        require(player.state().positionMs==ms && !player.state().playing,"SID paused seek position incorrect.");
    }
    player.toggleSolo(1);require(player.state().muteMask==4,"SID Solo did not restore mutes.");
    player.setMuteMask(0);player.setOutputScopesEnabled(true);player.seek(1000,true);
    until(app,[&]{return ready() && player.state().positionMs>=3000 && player.outputScopes().sequence;},"SID resume/Full mix failed.");
    player.setScopeMask(5);until(app,[&]{return player.scopes().mask==5;},"SID hide voice stalled scopes.");
    player.setScopeMask(7);until(app,ready,"SID show voice stalled scopes.");
    player.setScopesEnabled(false);until(app,[&]{return player.state().scopesSuspended;},"SID hidden scopes did not suspend.");
    player.setScopesEnabled(true);until(app,ready,"SID scopes did not resume.");
    window.loadFile(path,1);until(app,[&]{return ready() && player.state().info.song==1;},"SID subsong change failed.");
    require(!player.state().starvations && !player.state().outputErrors,"SID audio dropout.");
    window.refresh();window.grab().save(QDir(directory).filePath("native-sid-player.png"));
    window.scopeWindow().grab().save(QDir(directory).filePath("native-sid-scopes.png"));
    player.seek(3600000,true);QThread::msleep(50);
    QElapsedTimer closing;closing.start();player.shutdown();require(closing.elapsed()<2000,"SID exit blocked by seek.");
    log << "SID: live scopes, Full mix, Solo, seeks/subsongs, hide/reopen, uninterrupted audio and cancellation on shutdown PASS\n";log.flush();
    window.hide();window.scopeWindow().hide();
    for(int chips:{2,3}) {
        write(path,rsidFixture(chips));
        PlayerWindow multi;multi.player().setVolume(0);multi.show();multi.loadFile(path);
        auto &p=multi.player();const unsigned allVoices=(1u<<(chips*3))-1;
        auto prepared=[&]{auto s=p.state();auto f=p.scopes();return s.valid && !s.busy && s.error.isEmpty() && s.scopeError.isEmpty() && f.generation==s.generation && f.mask==allVoices && std::abs(s.positionMs-f.positionMs)<200;};
        until(app,prepared,"Multi-SID live scopes did not prepare.");multi.refresh();
        require(multi.scopeWindow().findChild<QCheckBox *>("scopeStereo")->isEnabled(),"Multi-SID stereo option disabled.");
        p.setOutputScopesEnabled(true);p.toggleSolo(chips*3-1);
        require(p.state().muteMask==~VoiceMask::bit(chips*3-1),"Multi-SID last voice Solo failed.");
        until(app,[&]{return prepared() && p.state().positionMs>=2500 && p.outputScopes().sequence;},"Multi-SID live Full mix stalled.");
        for(int ms:{5000,200,1000}) {p.seek(ms,false);until(app,prepared,"Multi-SID paused seek stalled.");}
        p.toggleSolo(chips*3-1);require(p.state().muteMask==0,"Multi-SID Solo restore failed.");
        multi.loadFile(path,1);until(app,[&]{return prepared() && p.state().info.song==1;},"Multi-SID subsong failed.");
        require(!p.state().starvations && !p.state().outputErrors,"Multi-SID audio dropout.");
        multi.refresh();multi.scopeWindow().grab().save(QDir(directory).filePath(QString("native-sid-%1-chips.png").arg(chips)));
        p.shutdown();multi.hide();multi.scopeWindow().hide();
    }
    write(path,romProbe(0));
    PlayerWindow warningWindow;warningWindow.player().setVolume(0);warningWindow.show();warningWindow.loadFile(path);
    until(app,[&]{warningWindow.refresh();return warningWindow.findChild<QMessageBox *>("romWarning");},"Required ROM warning not shown.");
    auto warning=warningWindow.findChild<QMessageBox *>("romWarning");
    require(warning->text().contains("kernal.rom") && warning->text().contains(QDir::toNativeSeparators(romRoot.path())),"ROM warning omits filename/directory.");
    warning->accept();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
    for(int i=0;i<10;++i) warningWindow.refresh();
    require(!warningWindow.findChild<QMessageBox *>("romWarning"),"ROM warning repeats for the same load.");
    write(path,rsidFixture());warningWindow.loadFile(path);
    until(app,[&]{auto s=warningWindow.player().state();return s.valid && !s.busy && s.positionMs>1000 && s.error.isEmpty();},"Self-contained RSID recovery failed.");
    warningWindow.refresh();require(!warningWindow.findChild<QMessageBox *>("romWarning"),"Self-contained RSID produced ROM warning.");
    warningWindow.player().shutdown();
    log << "Multi-SID: live 6/9 cards, stereo, last voice Solo, Full mix, seeks/subsongs, audio continuity; one-shot ROM warning and recovery PASS\n";log.flush();
}

void checkSidFiles(QApplication &app,const QStringList &paths,QTextStream &log) {
    for(const auto &path:paths) {
        PlayerWindow window;window.player().setVolume(0);window.show();window.loadFile(path);
        auto &player=window.player();
        auto ready=[&]{auto s=player.state();auto f=player.scopes();return s.valid && !s.busy && s.error.isEmpty() && s.scopeError.isEmpty() && f.generation==s.generation && f.mask==7 && std::abs(s.positionMs-f.positionMs)<200;};
        until(app,ready,"Local SID failed to start.");
        until(app,[&]{return ready() && player.state().positionMs>=5000;},"Local SID playback/scopes stalled.");
        for(int ms:{30000,500}) {
            player.seek(ms,false);until(app,ready,"Local SID seek/scopes failed.");
            require(player.state().positionMs==ms,"Local SID seek position incorrect.");
        }
        player.toggleSolo(0);player.seek(500,true);
        until(app,[&]{return ready() && player.state().positionMs>2000;},"Local SID Solo resume failed.");
        require(!player.state().starvations && !player.state().outputErrors,"Local SID audio errors.");
        log << "Local SID: " << player.state().info.configuration.name << ", playback, 30s/backward seek, shared scopes, Solo; audio errors=0 PASS\n";log.flush();
        player.shutdown();
    }
}
