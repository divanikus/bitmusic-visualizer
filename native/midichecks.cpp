#include "miditrack.h"
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
        const auto sequence=MidiSequence::parse(bytes,SampleRate);
        require(sequence.endFrame==66150 && sequence.title=="MIDI test song","MIDI tempo/title parsing failed.");
        auto rejects=[&](QByteArray b) { try { MidiSequence::parse(b,SampleRate); return false; } catch(const std::exception &) { return true; } };
        require(rejects(bytes.left(bytes.size()-1)),"Truncated MIDI accepted.");
        auto bad=bytes; bad[9]=2; require(rejects(bad),"Format 2 MIDI accepted.");
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
        selectSoundFont("Test.sf2"); require(selectedSoundFont()=="Test.sf2","SoundFont selection did not persist.");
        const auto sparsePath=resources.filePath("sparse.mid"),emptyPath=resources.filePath("empty.mid");
        write(sparsePath,sparse); write(emptyPath,empty);
        auto sparseTrack=openMusicTrack(sparsePath);
        require(sparseTrack->info().emptyVoices==(VoiceMask::first(16)&~sparseUsed) && sparseTrack->info().voices.size()==16,
            "MIDI unused-card hints changed channel identities.");
        require(openMusicTrack("missing.mid",0,false,true,{}, {},sparseTrack->info().midiResources)->info().emptyVoices==sparseTrack->info().emptyVoices,
            "MIDI snapshot lost empty-channel hints.");
        auto audio=openMusicTrack(midi); auto capture=openMusicTrack(midi,0,true,true,{}, {},audio->info().midiResources);
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
            require(QFile::copy(args[real+1],resources.filePath("Real.sf2")),"Cannot prepare requested SoundFont check.");
            selectSoundFont("Real.sf2");
            auto actual=openMusicTrack(midi); speed.restart();
            require(pcm(*actual,SampleRate/2)!=QByteArray(SampleRate*2,0),"User SoundFont rendered silence.");
            advance(*actual,10*SampleRate); log<<"User SoundFont: 10s audio render "<<speed.elapsed()<<" ms PASS\n"; log.flush();
            const int tune=args.indexOf("--midi");
            if(tune>=0&&tune+1<args.size()) {
                speed.restart(); auto tuneTrack=openMusicTrack(args[tune+1]);
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
                window.findChild<QToolButton*>("refreshSoundFonts")->click();
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
            window.close(); log<<"MIDI live: controls, paused seek, Solo, repeat, hide/resume, zero audio errors PASS\n";
        }
        return 0;
    } catch(const std::exception &e) { log<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
