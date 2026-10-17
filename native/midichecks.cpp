#include "miditrack.h"
#include "mt32track.h"
#include "playerwindow.h"
#include "appsettings.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QPushButton>
#include <QToolButton>
#include <QTabWidget>
#include <QListWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QKeyEvent>
#include <QDir>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include <stdexcept>

void checkMidiSubsongErrors();
void checkGmMetadata(const std::shared_ptr<const MidiResources> &resources, QTextStream &log);
void checkSoundFontBankCache(const QString &directory,const QByteArray &fixture,QTextStream &log);

namespace {
void require(bool ok,const char *s) { if(!ok) throw std::runtime_error(s); }
QByteArray read(const QString &path) { QFile f(path); require(f.open(QIODevice::ReadOnly),"Missing MIDI fixture."); return f.readAll(); }
void write(const QString &path,const QByteArray &bytes) { QFile f(path); require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Cannot write MIDI fixture."); }
void advance(MusicTrack &t,qint64 to,int chunk=BlockFrames) {
    std::array<short,BlockFrames*2> out;
    while(t.frame()<to) t.render(out.data(),int(std::min<qint64>(chunk,to-t.frame())));
}
float peak(const QVector<float>&v) { float p=0; for(float x:v) p=std::max(p,std::abs(x)); return p; }
QByteArray pcm(MusicTrack&t,int frames) { QByteArray out(frames*4,0); t.render(reinterpret_cast<short*>(out.data()),frames); return out; }
void spin(QApplication &app,const std::function<bool()> &ready,int ms=6000) {
    QElapsedTimer timer; timer.start();
    while(!ready()&&timer.elapsed()<ms) { app.processEvents(); QThread::msleep(5); }
    require(ready(),"MIDI live state timed out.");
}
}
int runMidiChecks(QApplication &app,const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--midi-checks"); require(at>=0&&at+1<args.size(),"Supply MIDI fixture directory.");
        const QString directory=QDir(args[at+1]).absolutePath();
        QTemporaryDir resources(QDir(directory).filePath("midi-resources-XXXXXX")); require(resources.isValid(),"Cannot create test resource directory.");
        app.setProperty("settingsFileForTests",resources.filePath("settings.ini"));
        app.setProperty("themesDirectoryForTests",resources.filePath("themes"));
        app.setProperty("soundFontsDirectoryForTests",resources.path());
        const QString midi=QDir(directory).filePath("demo.mid"); const auto bytes=read(midi);
        checkMidiSubsongErrors();
        const auto sequence=MidiSequence::parse(bytes,SampleRate);
        require(sequence.endFrame==66150 && sequence.title=="MIDI test song","MIDI tempo/title parsing failed.");
        auto rejects=[&](QByteArray b) { try { MidiSequence::parse(b,SampleRate); return false; } catch(const std::exception &) { return true; } };
        require(rejects(bytes.left(bytes.size()-1)),"Truncated MIDI accepted.");
        auto bad=bytes; bad[9]=3; require(rejects(bad),"Unknown MIDI format accepted.");
        bad=bytes; bad[12]=0; bad[13]=0; require(rejects(bad),"Zero MIDI division accepted.");
        const auto smf=[](const QByteArray &events,int division=480) {
            QByteArray b=QByteArray::fromHex("4d546864000000060000000100004d54726b00000000");
            qToBigEndian(quint16(division),b.data()+12); qToBigEndian(quint32(events.size()),b.data()+18); return b+events;
        };
        const auto running=smf(QByteArray::fromHex("009045640048648360454000483c00ff2f00"));
        const auto format0=MidiSequence::parse(running,SampleRate);
        require(format0.events.size()==4&&format0.endFrame==22050,"MIDI format 0 / running status failed.");
        require(format0.noteChannels==1 && sequence.noteChannels==0xffff,"MIDI channel usage lost format 0/1 or running status.");
        // Controllers/programs/note-offs are not notes; key zero is a valid note.
        // Channel 16 starts late, while channel 10 carries percussion.
        const auto sparse=smf(QByteArray::fromHex("00c30500b4076400853c4000963c000090006400000083609f48640099246400ff2f00"));
        const VoiceMask sparseUsed=VoiceMask::bit(0)|VoiceMask::bit(9)|VoiceMask::bit(15);
        require(VoiceMask(MidiSequence::parse(sparse,SampleRate).noteChannels)==sparseUsed,"MIDI empty-channel detection lost a late note or counted setup/Note Off.");
        const auto empty=smf(QByteArray::fromHex("00c30500b4076400853c4000963c0000ff2f00"));
        require(!MidiSequence::parse(empty,SampleRate).noteChannels,"Note-free MIDI has active channels.");
        const auto smpte=MidiSequence::parse(smf(QByteArray::fromHex("009045641980450000ff2f00"),0xe728),SampleRate);
        require(smpte.endFrame==1103,"SMPTE MIDI timing failed.");
        require(rejects(smf(QByteArray::fromHex("00010100ff2f00"))),"Invalid running status accepted.");
        require(!rejects(smf(QByteArray::fromHex("00ff2101010090456400ff2f00"))),"Single nonzero MIDI port rejected.");
        require(rejects(smf(QByteArray::fromHex("0090456400ff2101010091456400ff2f00"))),"Multiple MIDI ports accepted.");
        require(!rejects(smf(QByteArray::fromHex("00f00541101612f700ff2101010091456400ff2f00"))),"Unaddressed SysEx did not follow the sole MIDI port.");
        require(rejects(smf(QByteArray::fromHex("00ff21010000f00541101612f700ff2101010091456400ff2f00"))),"Explicit SysEx port conflict accepted.");
        const auto mt=smf(QByteArray::fromHex("00f00541101612f700ff2f00"));
        require(MidiSequence::parse(mt,SampleRate).mt32,"MT-32 SysEx was not recognized.");
        bool rejectedMt=false; try { MidiTrack track("test.mid",mt,0,false,false); }
        catch(const std::exception &e) { rejectedMt=QByteArray(e.what()).contains("MT-32"); }
        require(rejectedMt,"MT-32 file silently routed to GM.");
        bool missing=false; try { openMusicTrack(midi); } catch(const std::exception &e) { missing=QByteArray(e.what()).startsWith("SoundFont required:"); if(!missing) log<<"Unexpected load error: "<<e.what()<<'\n'; }
        require(missing,"Missing SoundFont did not produce an actionable error.");
        write(resources.filePath("Test.sf2"),read(QDir(directory).filePath("fixture-gm.sf2")));
        checkSoundFontBankCache(resources.path(),read(resources.filePath("Test.sf2")),log);
        selectSoundFont("Test.sf2"); require(selectedSoundFont()=="Test.sf2","SoundFont selection did not persist.");
        // SMF 2 patterns have independent tempo, ports, titles, notes and engine hints.
        auto patterns = smf(QByteArray::fromHex("00ff03014100ff51030f424000ff21010000903c648360803c0000ff2f00"));
        patterns[9]=2; patterns[11]=2;
        patterns += smf(QByteArray::fromHex("00ff03014200ff21010100914364836081430000ff2f00")).mid(14);
        const auto parsed = MidiSequence::parseSongs(patterns,SampleRate);
        require(parsed.size()==2 && parsed[0].endFrame==SampleRate && parsed[1].endFrame==SampleRate/2 &&
                parsed[0].title=="A" && parsed[1].title=="B" && parsed[0].noteChannels==1 && parsed[1].noteChannels==2,
                "Format 2 patterns shared tempo, ports, titles or notes.");
        require(rejects(patterns.left(patterns.size()-1)),"Malformed unselected MIDI pattern accepted.");
        const auto patternPath=resources.filePath("patterns.mid"); write(patternPath,patterns);
        auto second=openMusicTrack(patternPath,1,true,true);
        require(second->info().song==1 && second->info().songs==2 && second->info().playlist.size()==2 &&
                second->info().title=="B" && second->info().durationMs==2500 && second->info().playlist[0].durationMs==3000 &&
                second->info().emptyVoices==(VoiceMask::first(16)&~VoiceMask::bit(1)),"Format 2 subsong metadata is incorrect.");
        advance(*second,SampleRate/4);
        require(second->notePitches(0).isEmpty() && second->notePitches(1).size()==1,"Another pattern leaked into MIDI synthesis.");
        auto patternSnapshot=openMusicTrack("missing.mid",1,false,true,{}, {},second->info().midiResources);
        advance(*patternSnapshot,SampleRate/4);
        require(patternSnapshot->info().song==1 && pcm(*patternSnapshot,1024)==pcm(*second,1024),"Format 2 capture lost its selected pattern.");
        second->seek(0); advance(*second,SampleRate/4);
        require(second->notePitches(1).size()==1,"Format 2 backward seek lost selected notes.");
        bool badSong=false; try { openMusicTrack(patternPath,2); } catch(const std::exception &e) { badSong=QByteArray(e.what()).contains("subsong"); }
        require(badSong,"Out-of-range MIDI pattern accepted.");
        auto mixed=smf(QByteArray::fromHex("00903c648360803c0000ff2f00")); mixed[9]=2; mixed[11]=2;
        mixed+=mt.mid(14);
        selectMidiEngine("auto");
        require(bool(openMidiTrack("mixed.mid",mixed,0,true,false)->info().midiResources),"MT-32 hint leaked across independent patterns.");
        bool wantsRom=false; try { openMidiTrack("mixed.mid",mixed,1,true,false); }
        catch(const std::exception &e) { wantsRom=QByteArray(e.what()).startsWith("MT-32 ROM required:"); }
        require(wantsRom,"Selected MT-32 pattern was not routed to Munt.");
        log << "SMF 2: independent tempo/ports/notes, subsong metadata, capture, seek, bounds and per-pattern engine selection PASS\n";
        // The later bank overrides matching programs, with fallback to earlier banks.
        const auto bank=read(resources.filePath("Test.sf2")); auto overlay=bank;
        const auto phdr=overlay.indexOf("phdr")+8; require(phdr>=8,"Fixture has no preset headers.");
        overlay.replace(phdr,20,QByteArray("Override").leftJustified(20,'\0'));
        qToLittleEndian(quint16(81),overlay.data()+phdr+38+20);
        qToLittleEndian(quint16(1),overlay.data()+phdr+76+20);
        write(resources.filePath("Base.sf2"),bank); write(resources.filePath("Overlay.sf2"),overlay);
        write(resources.filePath("Set.sflist"),QByteArray::fromHex("efbbbf")+"# Banks\r\nBase.sf2\r\n\"Overlay.sf2\"\r\n");
        selectSoundFont("Set.sflist"); require(selectedSoundFont()=="Set.sflist","SFLIST selection did not persist.");
        const auto layeredPath=resources.filePath("layered.mid");
        write(layeredPath,smf(QByteArray::fromHex("00c15000903c6400914364009924648360803c00008143000089240000ff2f00")));
        auto layered=openMusicTrack(layeredPath,0,true,true); advance(*layered,SampleRate/5);
        require(layered->info().midiResources->soundFonts.size()==2 && layered->instrumentNames(layered->frame()).value(0)=="Override" &&
                layered->instrumentNames(layered->frame()).value(1)=="Lead" && layered->instrumentNames(layered->frame()).value(9)=="Drums",
                "SFLIST precedence or fallback across melodic/percussion banks failed.");
        QFile::remove(resources.filePath("Base.sf2")); QFile::remove(resources.filePath("Overlay.sf2"));
        const auto layeredPcm=pcm(*layered,1024);
        layered->seek(0); advance(*layered,SampleRate/5);
        require(pcm(*layered,1024)==layeredPcm,"SFLIST rewind reread deleted bank files.");
        auto layeredCopy=openMusicTrack("missing.mid",0,false,true,{}, {},layered->info().midiResources);
        advance(*layeredCopy,SampleRate/5); require(pcm(*layeredCopy,1024)==layeredPcm,"SFLIST capture did not share immutable bank snapshots.");
        for (const QByteArray &list : {QByteArray(), QByteArray("Missing.sf2"), QByteArray("Test.sf2\nTest.sf2"),
                QByteArray("Other.sflist"), QByteArray("0,0|Test.sf2"), QByteArray::fromHex("fffe")}) {
            write(resources.filePath("Invalid.sflist"),list); bool rejected=false;
            try { loadSoundFontBanks("Invalid.sflist"); } catch(const std::exception &) { rejected=true; }
            require(rejected,"Invalid or unsupported SoundFont list accepted.");
        }
        selectSoundFont("Test.sf2");
        log << "SFLIST: UTF-8/BOM/relative paths, layered program priority, fallback, saved choice, snapshot seeks and validation PASS\n";
        const auto sparsePath=resources.filePath("sparse.mid"),emptyPath=resources.filePath("empty.mid");
        write(sparsePath,sparse); write(emptyPath,empty);
        auto sparseTrack=openMusicTrack(sparsePath);
        require(sparseTrack->info().emptyVoices==(VoiceMask::first(16)&~sparseUsed) && sparseTrack->info().voices.size()==16,
            "MIDI unused-card hints changed channel identities.");
        require(openMusicTrack("missing.mid",0,false,true,{}, {},sparseTrack->info().midiResources)->info().emptyVoices==sparseTrack->info().emptyVoices,
            "MIDI snapshot lost empty-channel hints.");
        auto audio=openMusicTrack(midi); auto capture=openMusicTrack(midi,0,true,true,{}, {},audio->info().midiResources);
        checkGmMetadata(audio->info().midiResources,log);
        require(audio->info().songs==1 && audio->info().voices.size()==16 && audio->info().stereoOutput &&
                audio->info().durationMs==3500 && capture->hasTaps(),"General MIDI metadata/capture incorrect.");
        advance(*audio,SampleRate/5); advance(*capture,SampleRate/5);
        require(audio->instrumentNames(audio->frame()).value(0)=="Sine" &&
                audio->instrumentNames(audio->frame()).value(9)=="Drums","SoundFont instrument names missing.");
        QVector<float> left,right;
        for(int c=0;c<16;++c) {
            require(capture->readTap(c,left,right)&&std::max(peak(left),peak(right))>20,"MIDI channel is silent or missing.");
            if(c==0) require(peak(left)>100 && peak(right)<1,"MIDI left pan wrong.");
            // SoundFont's default CC10 modulator maps 127 just short of +500.
            if(c==1) require(peak(right)>100 && peak(left)<peak(right)*.02f,"MIDI right pan wrong.");
        }
        require(capture->notePitches(0).size()==3&&std::abs(capture->notePitches(0)[0]-440)<.1,"MIDI chord metadata wrong.");
        advance(*capture,SampleRate/2); require(!capture->notePitches(0).isEmpty(),"Sustain disappeared from keyboard.");
        advance(*capture,SampleRate*4/5);
        require(std::abs(capture->notePitches(1).value(0)-float(440*std::pow(2.,1.5/12)))<.1,"MIDI channel pitch bend missing.");
        const auto expected=pcm(*audio,1024);
        audio->seek(0); advance(*audio,SampleRate/5,37);
        require(pcm(*audio,1024)==expected,"MIDI rewind or irregular block sizes changed PCM.");
        const auto keep=audio->frame(); require(!audio->seek(SampleRate,[]{return true;})&&audio->frame()==keep,"Cancelled MIDI seek advanced.");
        audio->mute(~VoiceMask{}); require(pcm(*audio,2048)==QByteArray(8192,0),"Muted MIDI/effects leaked audio.");
        audio->mute(0); require(pcm(*audio,1024)!=QByteArray(4096,0),"MIDI unmute failed to restore held notes.");
        // Every Solo is truly isolated, including its chorus/reverb return.
        for(int c=0;c<16;++c) {
            audio->seek(0); audio->mute(~VoiceMask::bit(c)); advance(*audio,SampleRate/5);
            require(pcm(*audio,256)!=QByteArray(1024,0),"MIDI Solo channel silent.");
        }
        auto snapshot=audio->info().midiResources;
        require(QFile::remove(resources.filePath("Test.sf2")),"Cannot remove generated test bank.");
        capture=openMusicTrack("removed.mid",0,false,true,{}, {},snapshot); advance(*capture,SampleRate/5);
        require(capture->readTap(0,left,right)&&peak(left)>100,"MIDI resource snapshot lost its bank or sequence.");
        capture->seek(0); advance(*capture,SampleRate/5); require(capture->readTap(0,left,right)&&peak(left)>100,"Snapshot rewind failed.");
        require(selectedSoundFont()=="Test.sf2","Missing selected bank silently changed.");
        auto changed=read(QDir(directory).filePath("fixture-gm.sf2"));
        const auto sample=changed.indexOf("smpl")+8; require(sample>8,"Bad authored SoundFont fixture.");
        for(int i=0;i<100;++i) qToLittleEndian(qint16(-qFromLittleEndian<qint16>(changed.constData()+sample+2*i)),changed.data()+sample+2*i);
        write(resources.filePath("Test.sf2"),changed);
        auto replacement=openMusicTrack(midi,0,false,true); advance(*replacement,SampleRate/5);
        QVector<float> newLeft,newRight; replacement->readTap(0,newLeft,newRight);
        require(newLeft!=left,"Changed bank reused cached samples from the previous SoundFont.");
        write(resources.filePath("Test.sf2"),read(QDir(directory).filePath("fixture-gm.sf2")));
        QElapsedTimer speed; speed.start(); capture->seek(0); advance(*capture,10*SampleRate);
        log<<"MIDI parser, stereo taps / all 16 channels, sustain notes, mute/Solo/effects, rewind, cancellation, snapshots PASS; 10s capture "<<speed.elapsed()<<" ms\n"; log.flush();
        auto longTrack=openMusicTrack(QDir(directory).filePath("long-seek.mid"),0,false,true);
        require(longTrack->info().durationMs==2702000,"Long MIDI duration changed.");
        speed.restart(); require(longTrack->seek(12*SampleRate),"Long seek failed.");
        require(longTrack->instrumentNames(12*SampleRate).value(0)=="Lead","Seek lost current instrument.");
        for(int c=0;c<9;++c) {
            const bool held=c==0||c==1||c==2||c==8;
            require(longTrack->notePitches(c).isEmpty()!=held,"Seek lost held keys/pedals or restored ended notes.");
            if(held) { longTrack->readTap(c,left,right); require(std::max(peak(left),peak(right))>20,"Chased held note has no waveform."); }
        }
        require(std::abs(longTrack->notePitches(0).value(0)-float(440*std::pow(2.,.5/12)))<.1,"Seek lost pitch bend.");
        require(longTrack->seek(16*SampleRate),"Seek past pedal release failed.");
        for(int c=0;c<9;++c) require(longTrack->notePitches(c).isEmpty(),"Seek left a stuck key.");
        require(longTrack->seek(2600LL*SampleRate),"Late MIDI seek failed.");
        log<<"45-minute MIDI seek to 43:20: "<<speed.elapsed()<<" ms (including held-note checks)\n"; log.flush();
        int cancelCalls=0;
        require(!longTrack->seek(20*SampleRate,[&]{return ++cancelCalls>8;}),"Mid-chase cancellation ignored.");
        require(longTrack->seek(12*SampleRate)&&!longTrack->notePitches(1).isEmpty(),"Seek after cancellation damaged MIDI state.");
        longTrack->seek(0); advance(*longTrack,5*SampleRate+1);
        require(longTrack->instrumentNames(4*SampleRate).value(0)=="Sine"&&
                longTrack->instrumentNames(5*SampleRate).value(0)=="Lead","Instrument read-ahead leaked into past labels.");
        const int real=args.indexOf("--soundfont");
        if(real>=0&&real+1<args.size()) {
            const auto source=QFileInfo(args[real+1]).absoluteFilePath();
            const auto savedDirectory=app.property("soundFontsDirectoryForTests");
            app.setProperty("soundFontsDirectoryForTests",QFileInfo(source).absolutePath());
            selectSoundFont(QFileInfo(source).fileName());
            speed.restart(); auto actual=openMusicTrack(midi);
            log<<"User SoundFont initial load: "<<speed.elapsed()<<" ms\n"; log.flush();
            speed.restart();
            require(pcm(*actual,SampleRate/2)!=QByteArray(SampleRate*2,0),"User SoundFont rendered silence.");
            advance(*actual,10*SampleRate); log<<"User SoundFont: 10s audio render "<<speed.elapsed()<<" ms PASS\n"; log.flush();
            for(int pass=0;pass<3;++pass) {
                speed.restart(); auto next=openMusicTrack(midi);
                log<<"User SoundFont next track "<<pass+1<<": "<<speed.elapsed()<<" ms\n"; log.flush();
                const auto &before=actual->info().midiResources->soundFonts;
                const auto &after=next->info().midiResources->soundFonts;
                require(before.size()==after.size(),"Next track lost SoundFont banks.");
                for(int i=0;i<before.size();++i)
                    require(before[i].bytes.constData()==after[i].bytes.constData(),"Next MIDI track reread unchanged SoundFont bytes.");
                require(pcm(*next,SampleRate/2)!=QByteArray(SampleRate*2,0),"Next MIDI track rendered silence.");
                actual=std::move(next);
            }
            const int tune=args.indexOf("--midi");
            if(tune>=0&&tune+1<args.size()) {
                const auto songCount=openMusicTrack(args[tune+1])->info().songs;
                for(int song=0;song<songCount;++song) {
                speed.restart(); auto tuneTrack=openMusicTrack(args[tune+1],song);
                log<<"Local MIDI load: "<<speed.elapsed()<<" ms; duration "<<tuneTrack->info().durationMs<<" ms\n"; log.flush();
                QStringList used;
                for(int i=0;i<tuneTrack->info().voices.size();++i) if(!tuneTrack->info().emptyVoices.test(i)) used<<QString::number(i+1);
                log<<"Local MIDI channels with notes: "<<used.join(", ")<<'\n';
                for(int percent:{50,95,5}) {
                    speed.restart(); const qint64 target=qint64(tuneTrack->info().durationMs)*SampleRate/1000*percent/100;
                    require(tuneTrack->seek(target),"Local MIDI seek failed.");
                    require(tuneTrack->frame()==target,"Local MIDI seek landed at wrong time.");
                    log<<"Local MIDI seek "<<percent<<"%: "<<speed.elapsed()<<" ms\n"; log.flush();
                }
                }
            }
            app.setProperty("soundFontsDirectoryForTests",savedDirectory);
            selectSoundFont("Test.sf2");
        }
        if(!args.contains("--decoder-only")) {
            write(resources.filePath("Other.sf2"),read(resources.filePath("Test.sf2")));
            PlayerWindow window; window.player().setVolume(0); window.show(); window.loadFile(midi);
            spin(app,[&]{auto s=window.player().state();return s.valid&&!s.busy&&s.playing&&window.player().scopes().mask==VoiceMask::first(16);});
            auto &player=window.player();
            require(player.state().error.isEmpty()&&player.state().scopeError.isEmpty(),"MIDI live error.");
            auto combo=window.findChild<QComboBox*>("soundFont"); require(combo&&combo->isVisible()&&combo->currentData()=="Test.sf2","SoundFont controls missing.");
            const auto reselect=[&] {
                const auto before=player.state();
                QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,combo->currentIndex()));
                const auto engine=window.findChild<QComboBox*>("midiEngine");
                require(engine!=nullptr,"MIDI engine selector missing.");
                QMetaObject::invokeMethod(engine,"activated",Qt::DirectConnection,Q_ARG(int,engine->currentIndex()));
                const auto retained=loadSoundFontBanks("Test.sf2");
                window.findChild<QToolButton*>("refreshSoundFonts")->click();
                const auto refreshed=loadSoundFontBanks("Test.sf2");
                require(retained[0].bytes.constData()!=refreshed[0].bytes.constData(),"Refresh button kept cached bank input.");
                const auto after=player.state();
                require(after.generation==before.generation&&!after.busy&&after.playing==before.playing&&
                    (before.playing||after.positionMs==before.positionMs),"Reselecting the current MIDI option or refreshing banks restarted playback.");
            };
            reselect();
            player.seek(800,false); spin(app,[&]{auto s=player.state();return !s.busy&&!s.playing&&s.positionMs==800&&player.scopes().positionMs==800;});
            reselect();
            const auto beforeBankChange=player.state().generation;
            const int other=combo->findData("Other.sf2"); require(other>=0,"Second test bank missing.");
            combo->setCurrentIndex(other); QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,other));
            spin(app,[&]{auto s=player.state();return s.generation>beforeBankChange&&!s.busy&&s.playing&&s.info.midiResources&&s.info.midiResources->soundFontName=="Other.sf2";});
            require(selectedSoundFont()=="Other.sf2","Changed SoundFont was not saved.");
            log<<"MIDI choices: current bank/engine preserve play/pause; a different bank reloads PASS\n";
            player.toggleSolo(1); require(player.state().soloChannel==1,"Live MIDI Solo failed.");
            player.seek(0,true); spin(app,[&]{return !player.state().busy&&player.state().positionMs>100;});
            player.setRepeat(true); player.seek(3400,true);
            spin(app,[&]{auto s=player.state();return !s.busy&&s.positionMs<1000&&s.playing;});
            require(player.state().starvations==0&&player.state().outputErrors==0,"Live MIDI audio errors.");
            window.scopeWindow().hide(); spin(app,[&]{return player.state().scopesSuspended;});
            window.scopeWindow().show(); spin(app,[&]{return !player.state().scopesSuspended&&player.scopes().mask==VoiceMask::first(16);});
            player.seek(200,false);
            spin(app,[&]{return !player.state().busy&&player.state().positionMs==200&&player.scopes().generation==player.state().generation;});
            window.refresh(); app.processEvents();
            require(window.findChild<QToolButton*>("voice0")->text().contains("Sine"),"Channel button lacks instrument name.");
            require(player.scopes().instruments.value(0)=="Sine","Scope frame lacks instrument name.");
            window.grab().save(QDir(directory).filePath("midi-player.png"));
            window.findChild<QTabWidget*>("tabs")->setCurrentIndex(1); app.processEvents();
            window.grab().save(QDir(directory).filePath("midi-channels.png"));
            window.scopeWindow().grab().save(QDir(directory).filePath("midi-scopes.png"));
            player.setRepeat(false);
            window.loadFile(QDir(directory).filePath("fractional-end.mid"));
            spin(app,[&]{return !player.state().busy&&player.state().info.durationMs==3525;});
            for(int restart=0;restart<3;++restart) {
                player.seek(3400,true);
                spin(app,[&]{return !player.state().busy&&!player.state().playing;});
                require(player.state().positionMs==3525,"Natural endpoint lost a millisecond.");
                window.refresh();
                if(restart==0) window.findChild<QPushButton*>("primary")->click();
                else if(restart==1) { QKeyEvent key(QEvent::KeyPress,Qt::Key_F7,Qt::NoModifier); QApplication::sendEvent(&window,&key); }
                else player.seek(player.state().positionMs,true);
                spin(app,[&]{auto s=player.state();return !s.busy&&s.playing&&s.positionMs>0&&s.positionMs<1000;});
            }
            window.loadFile(QDir(directory).filePath("long-seek.mid"));
            spin(app,[&]{return !player.state().busy&&player.state().info.durationMs==2702000;});
            window.refresh(); window.scopeWindow().toggleSourceVisibility(3);
            const auto layout=window.scopeWindow().visibleChannels();
            player.seek(2600000,false);
            spin(app,[&]{auto s=player.state();auto f=player.scopes();return !s.busy&&!s.playing&&s.positionMs==2600000&&f.generation==s.generation&&f.positionMs==2600000;});
            require(player.scopes().instruments.value(0)=="Lead","Late paused scope seek lost instrument.");
            window.refresh();
            require(window.scopeWindow().visibleChannels()==layout && window.findChild<QToolButton*>("voice0")->text().contains("Lead"),
                    "Instrument change reset the layout or left stale channel labels.");
            log<<"Live MIDI: end restart (button/F7/transport), instrument labels and long audio/scope seek PASS\n";
            auto &scope=window.scopeWindow();
            window.loadFile(sparsePath);
            spin(app,[&]{return !player.state().busy&&player.state().info.path==sparsePath&&player.scopes().mask==sparseUsed;});
            window.refresh();
            auto list=scope.findChild<QListWidget*>("scopeChannelList");
            auto rows=scope.findChild<QSpinBox*>("gridRows"),columns=scope.findChild<QSpinBox*>("gridColumns");
            const auto checkSparse=[&] {
                require(list->count()==17,"Unused MIDI cards were removed instead of unchecked.");
                for(int i=0;i<16;++i) require((list->item(i)->checkState()==Qt::Checked)==sparseUsed.test(i),"Unused MIDI checkbox state incorrect.");
            };
            checkSparse();
            require(scope.visibleChannels()==QVector<int>({0,9,15})&&rows->value()==3&&columns->value()==1&&!player.state().muteMask,
                "MIDI initial grid or channel numbering/mutes changed incorrectly.");
            list->item(1)->setCheckState(Qt::Checked); player.seek(600,false);
            spin(app,[&]{return !player.state().busy&&!player.state().playing&&player.scopes().generation==player.state().generation;});
            window.refresh(); require(list->item(1)->checkState()==Qt::Checked,"Transport re-hid a manually enabled empty card.");
            scope.resetLayout(); checkSparse();
            auto keep=scope.findChild<QCheckBox*>("keepGrid"); keep->setChecked(true); rows->setValue(2); columns->setValue(2);
            window.loadFile(sparsePath); spin(app,[&]{return !player.state().busy;}); window.refresh(); checkSparse();
            require(rows->value()==2&&columns->value()==2,"MIDI auto-hide discarded Keep grid.");
            ScopePreset preset; preset.configuration=player.state().info.configuration; preset.rows=8; preset.columns=2;
            for(const auto &source:preset.configuration.sources) preset.cards.push_back({source});
            QString presetError; const auto presetPath=QDir(playerPresetsDirectory()).filePath("MIDI all.ini");
            QDir().mkpath(playerPresetsDirectory()); require(preset.save(presetPath,presetError),"Cannot write MIDI layout fixture.");
            QSettings settings(playerSettingsPath(),playerSettingsFormat());
            const auto defaultKey="PresetDefaults/"+preset.configuration.id;
            settings.setValue(defaultKey,"MIDI all.ini"); settings.sync();
            window.loadFile(sparsePath); spin(app,[&]{return !player.state().busy;}); window.refresh();
            require(list->count()==16&&list->item(1)->checkState()==Qt::Unchecked&&rows->value()==8&&columns->value()==2,
                "MIDI file default did not hide empty cards or lost its saved grid.");
            require(read(presetPath)==preset.encode(),"Auto-hide overwrote a saved preset.");
            auto presets=scope.findChild<QComboBox*>("scopePreset");
            QMetaObject::invokeMethod(presets,"activated",Qt::DirectConnection,Q_ARG(int,presets->currentIndex()));
            require(list->item(1)->checkState()==Qt::Checked,"Manual preset selection could not restore empty cards.");
            settings.remove(defaultKey); settings.sync(); keep->setChecked(false);
            window.loadFile(emptyPath); spin(app,[&]{return !player.state().busy&&player.state().info.path==emptyPath;}); window.refresh();
            require(scope.visibleChannels().isEmpty()&&list->count()==17&&rows->value()==1&&columns->value()==1,
                "Note-free MIDI did not produce a safe empty grid.");
            scope.findChild<QPushButton*>("showAllScopes")->click();
            require(list->item(1)->checkState()==Qt::Checked,"Show all did not restore empty MIDI cards.");
            log<<"MIDI empty cards: whole-file detection, late notes, stable IDs, manual restore, transport, Reset, Keep grid and presets PASS\n";
            window.setPlaylistRepeat(PlaylistRepeat::Off);
            window.loadFile(patternPath,1);
            // Changing resources while the first load is pending must keep pattern 2 as well.
            auto pendingBank=window.findChild<QComboBox *>("soundFont");
            pendingBank->setCurrentIndex(pendingBank->findData(pendingBank->currentData()=="Test.sf2" ? "Other.sf2" : "Test.sf2"));
            QMetaObject::invokeMethod(pendingBank,"activated",Qt::DirectConnection,Q_ARG(int,pendingBank->currentIndex()));
            spin(app,[&]{return !player.state().busy && player.state().info.path==patternPath && window.playlist().entries.size()==2;});
            require(player.state().info.song==1 && window.playlist().currentRow()==1,"MIDI pattern selection did not expand the playlist correctly.");
            auto bankChoice=window.findChild<QComboBox *>("soundFont");
            const auto oldGeneration=player.state().generation;
            const auto otherBank=bankChoice->currentData()=="Test.sf2" ? "Other.sf2" : "Test.sf2";
            bankChoice->setCurrentIndex(bankChoice->findData(otherBank));
            QMetaObject::invokeMethod(bankChoice,"activated",Qt::DirectConnection,Q_ARG(int,bankChoice->currentIndex()));
            spin(app,[&]{return !player.state().busy && player.state().generation>oldGeneration;});
            require(player.state().info.song==1,"Changing SoundFont jumped to the first MIDI pattern.");
            window.findChild<QPushButton *>("previous")->click();
            spin(app,[&]{return !player.state().busy && player.state().info.song==0;});
            window.findChild<QPushButton *>("next")->click();
            spin(app,[&]{return !player.state().busy && player.state().info.song==1;});
            require(player.state().error.isEmpty() && player.state().scopeError.isEmpty(),"MIDI pattern navigation failed.");
            log << "SMF 2 live: playlist expansion, Previous/Next and SoundFont reload keep the selected subsong PASS\n";
            // A genuine silent pattern (nonzero duration), followed by three used channels.
            auto intro = smf(QByteArray::fromHex("8360ff2f00")); intro[9]=2; intro[11]=2;
            intro += sparse.mid(14);
            const auto introPath=resources.filePath("silent-intro.mid"); write(introPath,intro);
            scope.resetLayout(); window.loadFile(introPath);
            spin(app,[&]{return !player.state().busy && player.state().info.path==introPath;});
            window.refresh();
            require(scope.visibleChannels().isEmpty() && rows->value()==1 && columns->value()==1,
                    "Silent first pattern did not hide its empty channels.");
            player.seek(2400,true);
            spin(app,[&]{return !player.state().busy && player.state().info.song==1 && player.scopes().mask==sparseUsed;});
            window.refresh(); checkSparse();
            require(scope.visibleChannels()==QVector<int>({0,9,15}) && rows->value()==3 && columns->value()==1 && !player.state().muteMask,
                    "Automatic pattern advance retained empty cards/grid or muted audio.");
            rows->setValue(3); columns->setValue(2);
            list->item(0)->setCheckState(Qt::Unchecked); scope.cycleSourceView(9);
            window.findChild<QPushButton *>("previous")->click();
            spin(app,[&]{return !player.state().busy && player.state().info.song==0;}); window.refresh();
            require(scope.visibleChannels().isEmpty(),"Silent pattern retained sounding cards.");
            window.findChild<QPushButton *>("next")->click();
            spin(app,[&]{return !player.state().busy && player.state().info.song==1;}); window.refresh();
            require(scope.visibleChannels()==QVector<int>({9,15}) && rows->value()==3 && columns->value()==2,
                    "Pattern switch discarded manual visibility or grid dimensions.");
            list->setCurrentRow(9);
            require(scope.findChild<QComboBox *>("cardView")->currentData().toInt()==int(ScopeView::Spectrum),
                    "Pattern switch discarded the card view.");
            list->item(1)->setCheckState(Qt::Checked); player.seek(0,false);
            spin(app,[&]{return !player.state().busy && !player.state().playing;}); window.refresh();
            require(list->item(1)->checkState()==Qt::Checked,"Same-pattern seek hid a manually enabled empty card.");
            log << "SMF 2 visibility: empty first pattern, automatic advance, capture, automatic/manual grids, manual hides and seek PASS\n";
            window.close(); log<<"MIDI live: controls, paused seek, Solo, repeat, hide/resume, zero audio errors PASS\n";
        }
        return 0;
    } catch(const std::exception &e) { log<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
