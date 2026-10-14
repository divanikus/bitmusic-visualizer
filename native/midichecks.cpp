#include "miditrack.h"
#include "window.h"
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
            PlayerWindow window; window.player().setVolume(0); window.show(); window.loadFile(midi);
            spin(app,[&]{auto s=window.player().state();return s.valid&&!s.busy&&s.playing&&window.player().scopes().mask==VoiceMask::first(16);});
            auto &player=window.player();
            require(player.state().error.isEmpty()&&player.state().scopeError.isEmpty(),"MIDI live error.");
            auto combo=window.findChild<QComboBox*>("soundFont"); require(combo&&combo->isVisible()&&combo->currentData()=="Test.sf2","SoundFont controls missing.");
            player.seek(800,false); spin(app,[&]{auto s=player.state();return !s.busy&&!s.playing&&s.positionMs==800&&player.scopes().positionMs==800;});
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
            window.close(); log<<"MIDI live: controls, paused seek, Solo, repeat, hide/resume, zero audio errors PASS\n";
        }
        return 0;
    } catch(const std::exception &e) { log<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
