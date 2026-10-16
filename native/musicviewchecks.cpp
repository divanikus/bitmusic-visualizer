#include "window.h"
#include "midisequence.h"
#include "trackertrack.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <algorithm>
#include <stdexcept>

namespace {
void require(bool v,const char *text) {if(!v) throw std::runtime_error(text);}
void pump(QApplication &app,int ms=100) {QElapsedTimer t;t.start();while(t.elapsed()<ms){app.processEvents();QThread::msleep(3);}}
QImage capture(QApplication &app,ScopeWindow &scope) {
    scope.update();pump(app);
    const auto gpu=dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
    return gpu&&gpu->isVisible()?gpu->grabFramebuffer():scope.grab().toImage();
}
void write(const QString &path,const QByteArray &bytes) {QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(bytes)==bytes.size(),"Cannot write test fixture.");}
QByteArray message(std::initializer_list<int> values) {QByteArray b;for(int v:values)b+=char(v);return b;}
}
int runMusicViewChecks(QApplication &app,const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--music-view-checks");require(at>=0&&at+1<args.size(),"Supply fixture directory.");
        QDir dir{QDir{args[at+1]}.absolutePath()}; QTemporaryDir settings(dir.filePath("music-view-settings-XXXXXX"));
        require(settings.isValid(),"Cannot isolate music view settings.");
        app.setProperty("settingsFileForTests",settings.filePath("settings.ini"));
        app.setProperty("themesDirectoryForTests",settings.filePath("themes"));
        app.setProperty("presetsDirectoryForTests",settings.filePath("presets"));
        MidiSequence sequence;sequence.endFrame=5*SampleRate;
        const auto event=[&](int ms,std::initializer_list<int> message){QByteArray b;for(int c:message)b+=char(c);sequence.events.push_back({qint64(ms)*SampleRate/1000,b});};
        event(0,{0xc0,1}); event(0,{0x90,60,100}); event(0,{0x91,64,90});
        event(500,{0x90,60,0}); event(700,{0xc0,5}); event(800,{0x90,67,80});
        event(1000,{0x81,64,0}); event(1500,{0xb0,123,0}); event(2000,{0x90,72,100});
        auto roll=midiNoteRoll(sequence);
        require(roll->notes.size()==4,"MIDI roll lost or duplicated notes.");
        require(roll->notes[0].end==SampleRate/2&&roll->notes[0].instrument==1&&roll->notes[2].instrument==5,"MIDI gates/program changes are wrong.");
        require(roll->visible(SampleRate/2,SampleRate,0).size()==1&&roll->visible(SampleRate/2,SampleRate,1).size()==1,"MIDI interval boundaries/source filtering failed.");
        require(roll->notes[2].end==SampleRate*3/2&&roll->notes[3].end==sequence.endFrame,"All-notes-off or dangling-note end failed.");
        const auto routed=midiNoteRoll(sequence,[](const MidiEvent &e){return (quint8(e.message[0])&15)==0?VoiceMask::bit(7):VoiceMask{};});
        require(routed->notes.size()==3&&routed->notes[0].source==7,"Part routing failed.");
        NoteRoll dense;for(int i=0;i<50000;++i)dense.notes.push_back({i,i+2,i%16,60,0,100});
        dense.notes.push_back({0,100000,0,40,0,100});dense.index();
        require(dense.visible(49000,49010).size()==13&&dense.visible(0,50000,-1,100).size()==100,"Indexed/bounded note lookup failed.");
        require(trackerNoteName(49)=="C-4"&&trackerNoteName(0)=="..."&&trackerNoteName(0x81)=="OFF","Tracker note formatting failed.");
        auto tracker=openMusicTrack(dir.filePath("demo.mod"));
        require(tracker->info().patterns&&tracker->info().patterns->channels==4,"Pattern data missing.");
        require(tracker->info().patterns->patterns[0].cells[0].instrument==1,"Pattern instrument missing.");
        tracker->seek(SampleRate);std::array<short,SampleRate> pcm;tracker->render(pcm.data(),SampleRate/2);
        require(tracker->patternPosition(SampleRate).row==8&&tracker->patternPosition(SampleRate*3/2).row==12,"Pattern uses decoder read-ahead instead of audible cursor.");
        tracker->seek(0);require(tracker->patternPosition(0).row==0,"Pattern rewind retained old rows.");
        QFile original(dir.filePath("demo.mod"));require(original.open(QIODevice::ReadOnly),"Missing MOD fixture.");auto bytes=original.readAll();
        for(int row=0;row<64;++row) for(int c=1;c<4;++c) {
            if(c==3&&row>=32)continue;
            const int offset=1084+(row*4+c)*4;for(int b=0;b<4;++b)bytes[offset+b]=0;
        }
        // Sample-only trigger must remain available even without a note period.
        bytes[1084+2*4+2]=char(0x10);
        const auto emptyFile=settings.filePath("empty-channel.mod");write(emptyFile,bytes);
        auto empty=openMusicTrack(emptyFile);
        require(empty->info().emptyVoices==VoiceMask::bit(1),"Empty channel detection hid a late entrance or sample-only trigger.");
        ScopePreset preset;preset.configuration=tracker->info().configuration;preset.rows=2;preset.columns=2;
        preset.cards.push_back({"mix",4,{2,2},true,false,true,false,true,2});
        QString error;const auto presetPath=settings.filePath("roundtrip.ini");require(preset.save(presetPath,error),"Preset save failed.");
        ScopePreset restored;require(restored.load(presetPath,error)&&restored.encode()==preset.encode(),"Music view preset roundtrip failed.");
        log<<"Music event checks PASS: MIDI gates/programs/routing/interval index, tracker pattern/read-ahead/rewind, empty/late/sample-only channels and presets.\n";log.flush();
        if(args.contains("--decoder-only"))return 0;
        ScopeWindow scope;scope.resize(1020,650);scope.show();
        PlayerState state;state.valid=true;state.generation=1;state.info=empty->info();state.pattern=empty->patternPosition(0);
        state.instruments={"Sine","","Saw","Triangle"};scope.present(state,{});pump(app);
        auto list=scope.findChild<QListWidget*>("scopeChannelList");auto view=scope.findChild<QComboBox*>("cardView");
        require(list->item(1)->checkState()==Qt::Unchecked&&list->item(3)->checkState()==Qt::Checked,"Empty tracker cards not initially hidden.");
        list->item(1)->setCheckState(Qt::Checked);require(list->item(1)->checkState()==Qt::Checked,"Empty card cannot be restored.");
        require(list->item(0)->text().contains("Channel 1"),"Stable source name missing.");
        scope.findChild<QPushButton*>("editLayout")->click();list->setCurrentRow(0);
        require(view->findData(4)>=0&&view->findData(3)<0,"Tracker view availability wrong.");
        for(int i=0;i<list->count();++i)list->item(i)->setCheckState(i==list->count()-1?Qt::Checked:Qt::Unchecked);
        list->setCurrentRow(list->count()-1);view->setCurrentIndex(view->findData(4));
        scope.findChild<QSpinBox*>("gridRows")->setValue(1);scope.findChild<QSpinBox*>("gridColumns")->setValue(1);
        scope.findChild<QPushButton*>("applyLayout")->click();
        auto first=capture(app,scope);require(first.save(dir.filePath("music-pattern.png")),"Cannot save pattern capture.");
        const auto gpu=dynamic_cast<ScopeGpu*>(scope.findChild<QQuickWidget*>("scopeGpu"));
        if(qEnvironmentVariableIsEmpty("BITMUSIC_SOFTWARE_SCOPES")) require(gpu&&gpu->isVisible(),"GPU music view unexpectedly fell back to CPU.");
        log<<"Music view renderer: "<<(gpu&&gpu->isVisible()?gpu->backend():QString("CPU"))<<'\n';
        state.pattern.row=16;scope.present(state,{});require(first!=capture(app,scope),"Paused pattern cursor did not update.");
        scope.findChild<QPushButton*>("editLayout")->click();scope.findChild<QCheckBox*>("patternCommands")->setChecked(true);
        scope.findChild<QPushButton*>("applyLayout")->click();capture(app,scope).save(dir.filePath("music-pattern-commands.png"));
        preset.cards[0].firstChannel=1; QDir().mkpath(settings.filePath("presets"));
        require(preset.save(settings.filePath("presets/Pattern.ini"),error),"Cannot save UI pattern preset.");
        auto presets=scope.findChild<QComboBox*>("scopePreset");
        scope.findChild<QPushButton*>("editLayout")->click();presets->showPopup();presets->hidePopup();
        const int saved=presets->findData("Pattern.ini");require(saved>=0,"Saved pattern preset missing.");
        presets->setCurrentIndex(saved);QMetaObject::invokeMethod(presets,"activated",Qt::DirectConnection,Q_ARG(int,saved));
        require(view->currentData().toInt()==4&&scope.findChild<QCheckBox*>("patternCommands")->isChecked()&&
            scope.findChild<QSpinBox*>("patternFirstChannel")->value()==2,"Pattern preset did not restore actual editor controls.");
        scope.findChild<QPushButton*>("applyLayout")->click();
        state.generation=2;state.info={};state.info.path="MIDI roll fixture";state.info.title="MIDI note roll";state.info.voices={"Channel 1","Channel 2"};
        // A dense authored sequence makes the moving view reviewable.
        MidiSequence demo;demo.endFrame=12*SampleRate;
        for(int i=0;i<24;++i)for(int ch=0;ch<2;++ch){
            const int note=ch?48+(i%4)*2:60+(i%8);
            demo.events.push_back({qint64(i)*SampleRate/2,message({0x90|ch,note,90})});
            demo.events.push_back({qint64(i)*SampleRate/2+SampleRate*3/8,message({0x80|ch,note,0})});
        }
        std::stable_sort(demo.events.begin(),demo.events.end(),[](auto &a,auto &b){return a.frame<b.frame;});
        demo.events.prepend({0,message({0xc1,10})});state.info.noteRoll=midiNoteRoll(demo);state.instruments={"Piano","Bass"};state.positionMs=700;
        scope.beginFile();scope.present(state,{});scope.findChild<QPushButton*>("editLayout")->click();
        require(view->findData(3)>=0&&view->findData(4)<0,"MIDI view availability wrong.");
        for(int i=0;i<list->count();++i)list->item(i)->setCheckState(i==list->count()-1?Qt::Checked:Qt::Unchecked);
        list->setCurrentRow(list->count()-1);view->setCurrentIndex(view->findData(3));
        scope.findChild<QSpinBox*>("gridRows")->setValue(1);scope.findChild<QSpinBox*>("gridColumns")->setValue(1);
        require(!scope.outputEnabled(),"Event-only Full mix unnecessarily enables audio capture.");
        scope.findChild<QPushButton*>("applyLayout")->click();first=capture(app,scope);first.save(dir.filePath("music-piano-roll.png"));
        state.positionMs=1200;scope.present(state,{});auto second=capture(app,scope);require(first!=second,"MIDI roll did not follow playhead.");
        state.muteMask=1;scope.present(state,{});require(second!=capture(app,scope),"MIDI roll ignores mute state.");
        scope.findChild<QPushButton*>("editLayout")->click();scope.findChild<QComboBox*>("rollDirection")->setCurrentIndex(1);
        scope.findChild<QPushButton*>("applyLayout")->click();capture(app,scope).save(dir.filePath("music-piano-roll-horizontal.png"));
        scope.resize(400,320);capture(app,scope).save(dir.filePath("music-piano-roll-small.png"));
        scope.resize(1020,650);scope.toggleFullscreen();capture(app,scope);scope.toggleFullscreen();
        scope.hide();scope.show();require(!capture(app,scope).isNull(),"Music overlay failed after hide/show/fullscreen.");
        scope.close();
        log<<"Music card checks PASS: empty-card restoration, view availability, Full mix, cursor/playhead updates, mutes, orientation, resize/fullscreen and images.\n";log.flush();
        NativePlayer player;player.setVolume(0);player.load(dir.filePath("demo.mod"));
        QElapsedTimer timer;timer.start();while((player.state().busy||!player.state().playing)&&timer.elapsed()<6000)pump(app,10);
        require(player.state().playing,"Live tracker did not start.");
        player.seek(1000,false);timer.restart();while(player.state().busy&&timer.elapsed()<6000)pump(app,10);
        require(!player.state().busy&&player.state().pattern.row==8,"Live paused tracker cursor not synchronized after seek.");
        player.seek(0,false);timer.restart();while(player.state().busy&&timer.elapsed()<6000)pump(app,10);
        require(player.state().pattern.row==0,"Live rewind pattern stale.");player.shutdown();
        log<<"Music live transport checks PASS.\n";return 0;
    } catch(const std::exception &e){log<<"FAIL: "<<e.what()<<'\n';return 1;}
}
