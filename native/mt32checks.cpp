#include "mt32track.h"
#include "window.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QPushButton>
#include <QDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
QByteArray read(const QString &path) { QFile f(path); require(f.open(QIODevice::ReadOnly),"Cannot read MT-32 test input."); return f.readAll(); }
void write(const QString &path,const QByteArray &data) { QFile f(path); require(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),"Cannot write test input."); }
QByteArray vlq(quint32 value) { QByteArray b(1,char(value&127)); while(value>>=7) b.prepend(char(128|(value&127))); return b; }
QByteArray fixture(int part=-1,bool sysex=true,int seconds=4) {
    QByteArray events;
    // Valid Roland reset. No ROM/recorded music content is embedded here.
    if(sysex) events=QByteArray::fromHex("00f00a411016127f00000100f7");
    for(int p=0;p<9;++p) if(part<0||p==part) {
        events.append(char(0)); events.append(char(0xc0|(p+1))); events.append(char(p==8?0:p*8));
        events.append(char(0)); events.append(char(0x90|(p+1))); events.append(char(p==8?36:60+p)); events.append(char(100));
    }
    events+=vlq(quint32(seconds*960)); events+=QByteArray::fromHex("ff2f00");
    QByteArray header=QByteArray::fromHex("4d546864000000060000000101e04d54726b00000000");
    qToBigEndian(quint32(events.size()),header.data()+18); return header+events;
}
float peak(const QVector<float> &v) { float p=0; for(float x:v) p=std::max(p,std::abs(x)); return p; }
QByteArray render(MusicTrack &track,int frames,int chunk=1024) {
    QByteArray result(frames*4,0); auto *out=reinterpret_cast<short *>(result.data());
    for(int done=0;done<frames;) { const int n=std::min(chunk,frames-done); track.render(out+done*2,n); done+=n; }
    return result;
}
void spin(QApplication &app,const std::function<bool()> &ready,int timeout=8000) {
    QElapsedTimer time; time.start();
    while(!ready()&&time.elapsed()<timeout) { app.processEvents(); QThread::msleep(5); }
    require(ready(),"MT-32 live state timed out.");
}
}
int runMt32Checks(QApplication &app,const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--mt32-checks"); require(at>=0&&at+1<args.size(),"Supply MT-32 test output directory.");
        const QDir directory{QDir(args[at+1]).absolutePath()}; QDir().mkpath(directory.path());
        QTemporaryDir temp(directory.filePath("mt32-check-XXXXXX")); require(temp.isValid(),"Cannot create test directory.");
        app.setProperty("settingsFileForTests",temp.filePath("settings.ini"));
        app.setProperty("themesDirectoryForTests",temp.filePath("themes"));
        app.setProperty("soundFontsDirectoryForTests",temp.filePath("soundfonts"));
        app.setProperty("romsDirectoryForTests",temp.filePath("roms"));
        const auto midi=temp.filePath("test.mid"); write(midi,fixture());
        selectMidiEngine("mt32"); require(selectedMidiEngine()=="mt32","MIDI engine not saved.");
        selectMidiEngine("auto"); require(selectedMidiEngine()=="auto","Auto engine not saved.");
        auto rejects=[&](const QByteArray &bytes,const char *reason) {
            write(midi,bytes);
            try { openMusicTrack(midi); return false; } catch(const std::exception &e) { return QByteArray(e.what()).contains(reason); }
        };
        require(rejects(fixture(),"MT-32 ROM required:"),"Auto did not request ROMs for MT-32 SysEx.");
        require(rejects(fixture(-1,false),"SoundFont required:"),"Auto requested ROMs for ordinary MIDI.");
        const auto namedError=[&](const QString &name,const char *reason) {
            const auto path=temp.filePath(name); QDir().mkpath(QFileInfo(path).absolutePath());
            write(path,fixture(-1,false));
            try { openMusicTrack(path); return false; }
            catch(const std::exception &e) { return QByteArray(e.what()).startsWith(reason); }
        };
        for(const auto *name:{"Soundtrack_MT_Attack.mid","song (mt32).MID","MT-32/song.mid"})
            require(namedError(name,"SoundFont required:"),"Auto used a filename or directory instead of MIDI contents.");
        selectMidiEngine("mt32"); require(rejects(fixture(-1,false),"MT-32 ROM required:"),"Manual MT-32 did not work without SysEx.");
        QDir().mkpath(playerMt32Directory());
        write(QDir(playerMt32Directory()).filePath("MT32_CONTROL.ROM"),QByteArray(65536,0));
        write(QDir(playerMt32Directory()).filePath("MT32_PCM.ROM"),QByteArray(524288,0));
        require(rejects(fixture(),"not recognized"),"Unrecognized ROM contents accepted.");
        log<<"MT-32 content-only selection, persistence and ROM checks PASS\n"; log.flush();
        const int rom=args.indexOf("--roms");
        if(rom<0 || rom+1>=args.size()) { log<<"Synthesis checks SKIPPED (no user-supplied ROMs).\n"; return 0; }
        for(const auto *name:{"MT32_CONTROL.ROM","MT32_PCM.ROM"})
            write(QDir(playerMt32Directory()).filePath(name),read(QDir(args[rom+1]).filePath(name)));
        write(midi,fixture()); selectMidiEngine("auto");
        auto audio=openMusicTrack(midi);
        const auto resources=audio->info().mt32Resources;
        require(resources && audio->info().voices.size()==9 && audio->info().durationMs==6000 && audio->info().stereoOutput,"MT-32 metadata incorrect.");
        require(!audio->info().emptyVoices,"Used MT-32 parts were marked empty.");
        auto capture=openMusicTrack(midi,0,true,true,{}, {},{},resources);
        require(render(*audio,SampleRate)==render(*capture,SampleRate,37),"Capture or chunk boundaries changed MT-32 audio.");
        require(!capture->instrumentNames(capture->frame()).value(0).isEmpty(),"MT-32 instrument names missing.");
        QVector<float> left,right;
        require(!capture->notePitches(0).isEmpty(),"MT-32 played-note metadata missing.");
        // Each part in isolation: other cards must be silent, its own gate must mute
        // the master without changing captured oscillator/partial evolution.
        for(int part=0;part<9;++part) {
            auto r=std::make_shared<Mt32Resources>(*resources); r->sequence=MidiSequence::parse(fixture(part),SampleRate);
            Mt32Track a(midi,{},0,false,true,r),b(midi,{},0,false,true,r); b.mute(VoiceMask::bit(part));
            require(a.info().emptyVoices==(VoiceMask::first(9)&~VoiceMask::bit(part)),"MT-32 empty-part detection confused MIDI channels and parts.");
            const auto audible=render(a,SampleRate/10),muted=render(b,SampleRate/10,17);
            require(audible!=QByteArray(audible.size(),0)&&muted==QByteArray(muted.size(),0),"MT-32 part mute leaked or source silent.");
            for(int c=0;c<9;++c) {
                QVector<float> bl,br;
                require(a.readTap(c,left,right)&&b.readTap(c,bl,br)&&left==bl&&right==br,"Mute changed part waveform or tap missing.");
                if(c==part) { log<<"Part "<<part+1<<" peak "<<std::max(peak(left),peak(right))<<'\n'; require(std::max(peak(left),peak(right))>5,"Active MT-32 part has no signal."); }
                else require(peak(left)==0&&peak(right)==0,"MT-32 part taps are not independent.");
            }
        }
        log<<"MT-32: nine independent parts, per-part mute, chunk invariance and capture transparency PASS\n"; log.flush();
        auto routedResources=std::make_shared<Mt32Resources>(*resources);
        auto &routedEvents=routedResources->sequence.events; routedEvents.clear();
        const auto routeEvent=[&](int seconds,const char *hex) { routedEvents.push_back({qint64(seconds)*SampleRate,QByteArray::fromHex(hex)}); };
        routeEvent(0,"913c64"); // Default Part 1; it must still sound before the later remapping.
        routeEvent(0,"943c00"); // Zero-velocity Note On is a release.
        routeEvent(0,"c508"); // A program alone does not use a part.
        routeEvent(5,"f04110161210000d0f0f45f7"); // Assign Parts 1 and 2 to MIDI channel 16.
        routeEvent(6,"9f4064");
        routeEvent(7,"f0411016127f00000100f7"); // Restore ROM defaults.
        routeEvent(8,"934364"); // Default Part 3 only after reset.
        routedResources->sequence.endFrame=10*SampleRate;
        Mt32Track routed(midi,{},0,false,true,routedResources);
        require(routed.info().emptyVoices==(VoiceMask::first(9)&~VoiceMask::first(3)),"MT-32 usage ignored later SysEx remapping or reset.");
        const auto roll=routed.info().noteRoll;
        require(roll&&roll->notes.size()==4&&roll->notes[0].source==0&&roll->notes[1].source==0&&
            roll->notes[2].source==1&&roll->notes[3].source==2,"MT-32 roll does not follow SysEx part assignments.");
        require(render(routed,SampleRate/10)!=QByteArray(SampleRate/10*4,0),"Usage scan leaked future routing into playback.");
        routedEvents={{0,QByteArray::fromHex("913c00")},{0,QByteArray::fromHex("b10764")}};
        Mt32Track noteFree(midi,{},0,false,false,routedResources);
        require(noteFree.info().emptyVoices==VoiceMask::first(9),"Note-free MT-32 parts were not marked empty.");
        log<<"MT-32 empty parts: late notes, shared channel assignments, SysEx remapping/reset and playback isolation PASS\n";
        capture->seek(0); auto fresh=openMusicTrack(midi,0,false,true,{}, {},{},resources);
        require(render(*capture,SampleRate/2)==render(*fresh,SampleRate/2),"Cold MT-32 rewind is nondeterministic.");
        require(!capture->seek(0,[]{return true;}),"MT-32 ignored cancellation.");
        // Resource snapshots survive file deletion, preference changes and a cold seek.
        QFile::remove(QDir(playerMt32Directory()).filePath("MT32_CONTROL.ROM")); QFile::remove(midi); selectMidiEngine("gm");
        auto retained=openMusicTrack(midi,0,false,true,{}, {},{},resources); require(retained->seek(SampleRate),"MT-32 resource snapshot lost.");
        selectMidiEngine("auto");
        write(QDir(playerMt32Directory()).filePath("MT32_CONTROL.ROM"),resources->control); write(midi,fixture());
        auto longResources=std::make_shared<Mt32Resources>(*resources);
        longResources->sequence=MidiSequence::parse(fixture(0,true,2700),SampleRate);
        Mt32Track longTrack(midi,{},0,false,true,longResources); QElapsedTimer speed; speed.start();
        require(longTrack.seek(2600LL*SampleRate)&&!longTrack.notePitches(0).isEmpty(),"Long MT-32 seek lost held note.");
        require(longTrack.readTap(0,left,right),"Long seek lost capture.");
        log<<"MT-32 45-minute fixture seek: "<<speed.elapsed()<<" ms PASS\n"; log.flush();
        auto pedalResources=std::make_shared<Mt32Resources>(*resources);
        auto &pedal=pedalResources->sequence; pedal.events.clear(); pedal.endFrame=30*SampleRate;
        const auto add=[&](int seconds,const char *hex) { pedal.events.push_back({qint64(seconds)*SampleRate,QByteArray::fromHex(hex)}); };
        add(0,"c118"); add(1,"913c64"); add(2,"b1407f"); add(3,"813c00");
        add(5,"914564"); add(12,"b14000"); add(15,"814500"); add(19,"914564"); add(20,"c110");
        Mt32Track held(midi,{},0,false,true,pedalResources);
        require(held.seek(10*SampleRate),"MT-32 pedal seek failed.");
        require(held.notePitches(0).size()==2,"MT-32 chase lost sustained/held keys.");
        require(held.seek(14*SampleRate)&&held.notePitches(0).size()==1,"MT-32 sustain release failed after chase.");
        require(held.seek(25*SampleRate)&&held.notePitches(0).isEmpty(),"MT-32 chase resurrected released notes.");
        log<<"MT-32 seek: program changes, held notes and sustain release PASS\n";
        int cancelCalls=0; require(!longTrack.seek(20*SampleRate,[&]{return ++cancelCalls>8;}),"MT-32 mid-seek cancellation ignored.");
        require(longTrack.seek(SampleRate),"MT-32 seek recovery failed.");
        const int tune=args.indexOf("--midi");
        if(tune>=0&&tune+1<args.size()) {
            selectMidiEngine("mt32"); speed.restart(); auto actual=openMusicTrack(args[tune+1],0,true,true);
            log<<"Local MT-32 MIDI load: "<<speed.elapsed()<<" ms; duration "<<actual->info().durationMs<<" ms\n"; speed.restart();
            qint64 firstNote=0;
            for(const auto &e:actual->info().mt32Resources->sequence.events)
                if((quint8(e.message[0])&0xf0)==0x90 && e.message.size()==3 && quint8(e.message[2])) { firstNote=e.frame; break; }
            log<<"Local MIDI first Note On: "<<double(firstNote)/SampleRate<<" s\n";
            if(firstNote>60*SampleRate) actual->seek(firstNote);
            const int count=int(firstNote-actual->frame()+10*SampleRate);
            const auto output=render(*actual,count); require(output!=QByteArray(output.size(),0),"Local MIDI is silent after first Note On.");
            log<<"Local MT-32 "<<double(count)/SampleRate<<"s audio + nine taps: "<<speed.elapsed()<<" ms\n";
            for(int percent:{50,95,5}) {
                speed.restart(); const auto target=qint64(actual->info().durationMs)*SampleRate/1000*percent/100;
                require(actual->seek(target)&&actual->frame()==target,"Local MT-32 seek failed.");
                log<<"Local seek "<<percent<<"%: "<<speed.elapsed()<<" ms\n";
            }
            log.flush();
        }
        if(!args.contains("--decoder-only")) {
            selectMidiEngine("auto"); PlayerWindow window; window.show(); window.player().setVolume(0); window.loadFile(midi);
            auto &player=window.player();
            spin(app,[&]{auto s=player.state();return !s.busy&&s.playing&&s.info.mt32Resources&&player.scopes().mask==VoiceMask(511);});
            auto *engine=window.findChild<QComboBox *>("midiEngine");
            const auto generation=player.state().generation;
            engine->setCurrentIndex(2); QMetaObject::invokeMethod(engine,"activated",Qt::DirectConnection,Q_ARG(int,2));
            spin(app,[&]{auto s=player.state();return s.generation>generation&&!s.busy&&s.playing&&s.info.mt32Resources;});
            require(selectedMidiEngine()=="mt32","UI engine choice was not saved.");
            engine->setCurrentIndex(0); QMetaObject::invokeMethod(engine,"activated",Qt::DirectConnection,Q_ARG(int,0));
            spin(app,[&]{auto s=player.state();return !s.busy&&s.playing&&s.info.mt32Resources;});
            window.refresh(); require(!window.findChild<QComboBox *>("soundFont")->isVisible(),"SoundFont selector shown for MT-32.");
            require(window.findChild<QComboBox *>("midiEngine")->currentData()=="auto","MIDI engine UI preference incorrect.");
            player.toggleSolo(2); spin(app,[&]{return player.state().soloChannel==2;}); player.toggleSolo(2);
            player.seek(2500,false); spin(app,[&]{auto s=player.state();auto f=player.scopes();return !s.busy&&!s.playing&&s.positionMs==2500&&f.generation==s.generation&&f.positionMs==2500&&f.mask==VoiceMask(511);});
            player.seek(5900,true); spin(app,[&]{auto s=player.state();return !s.busy&&!s.playing&&s.positionMs==6000;});
            window.refresh(); window.findChild<QPushButton *>("primary")->click();
            spin(app,[&]{auto s=player.state();return !s.busy&&s.playing&&s.positionMs>0&&s.positionMs<1000;});
            player.setRepeat(true); player.seek(5900,true);
            spin(app,[&]{auto s=player.state();return !s.busy&&s.playing&&s.positionMs>0&&s.positionMs<1000;});
            spin(app,[&]{auto s=player.state();auto f=player.scopes();return !s.busy&&s.playing&&s.positionMs>400&&s.positionMs<2000&&f.generation==s.generation&&f.mask==VoiceMask(511);});
            window.refresh(); window.grab().save(directory.filePath("mt32-player.png"));
            window.scopeWindow().grab().save(directory.filePath("mt32-scopes.png"));
            require(player.state().starvations==0&&player.state().outputErrors==0,"MT-32 live audio underrun.");
            window.close(); log<<"MT-32 live: nine scopes, MIDI controls, Solo, paused seek, end restart, repeat PASS\n";
        }
        log<<"MT-32 checks PASS\n"; return 0;
    } catch(const std::exception &e) { log<<"FAIL: "<<e.what()<<'\n'; log.flush(); return 1; }
}
