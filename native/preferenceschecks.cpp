#include "playerwindow.h"
#include "playersettings.h"
#include "appsettings.h"
#include "resourcestatus.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeEdit>
#include <QTreeWidget>
#include <QtEndian>
#include <array>
#include <stdexcept>

namespace {
void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
void spin(QApplication &app,const std::function<bool()> &ready,const char *message,int timeout=10000) {
    QElapsedTimer timer;timer.start();while(!ready() && timer.elapsed()<timeout){app.processEvents();QThread::msleep(5);}require(ready(),message);
}
void write(const QString &path,const QByteArray &data) {
    QFile f(path);require(f.open(QIODevice::WriteOnly) && f.write(data)==data.size(),"Preference fixture write failed.");
}
QByteArray fixture(bool pokey,bool loop) {
    // Original tone with a one-second intro and one-second body; no declared time.
    QByteArray data(pokey?0xc0:0x40,'\0');data.replace(0,4,"Vgm ");
    auto word=[&](int offset,quint32 v){qToLittleEndian(v,data.data()+offset);};
    word(8,pokey?0x161:0x150);word(pokey?0xb0:0x0c,pokey?1789790:3579545);word(0x34,data.size()-0x34);
    data+=QByteArray::fromHex(pokey?"bb0032bb01ab":"508d500f5094");
    data+=QByteArray::fromHex("6144ac");const int loopAt=data.size();
    data+=QByteArray::fromHex("6144ac66");word(4,data.size()-4);
    if(loop)word(0x1c,loopAt-0x1c);
    return data;
}
void checkResourceInspection(const QDir &scratch, QTextStream &log) {
    auto rows = inspectPlayerResources();
    const QStringList names{"MT32_CONTROL.ROM", "MT32_PCM.ROM", "kernal.rom", "basic.rom",
                            "chargen.rom", "ym2608_adpcm_rom.bin", "SoundFonts (*.sf2)"};
    require(rows.size() == names.size(), "Resource inspection changed the catalog size.");
    for (int i = 0; i < rows.size(); ++i)
        require(rows[i].file == names[i] && rows[i].status == "Missing" && !rows[i].found && rows[i].directory == (i == 6),
            "Resource inspection changed catalog order, kind or missing status.");
    require(!QFileInfo::exists(scratch.filePath("roms")) && !QFileInfo::exists(scratch.filePath("soundfonts")),
        "Resource inspection created directories.");
    const auto control = rows[0].path, kernal = rows[2].path, banks = rows[6].path;
    require(QDir().mkpath(QFileInfo(control).absolutePath()) && QDir().mkpath(QFileInfo(kernal).absolutePath()) &&
        QDir().mkpath(banks), "Resource-check directory failed.");
    for (int size : {0, 1, 2 * 1024 * 1024, 2 * 1024 * 1024 + 1}) {
        write(control, QByteArray(size, 'x'));
        const auto status = inspectPlayerResources()[0];
        const bool accepted = size > 0 && size <= 2 * 1024 * 1024;
        require(status.found == accepted && status.status == (accepted ? "Found" : "Invalid size"),
            "MT-32 presence check changed its size bounds.");
        if (accepted) require(status.detail.contains("Munt checks ROM identity"), "MT-32 presence claimed content validation.");
    }
    write(kernal, QByteArray(8191, 'x'));
    require(inspectPlayerResources()[2].status == "Invalid size", "Fixed-size ROM accepted a short image.");
    const QByteArray contents(8192, 'x'); write(kernal, contents);
    const auto fixed = inspectPlayerResources()[2];
    require(fixed.found && fixed.status == "Found (size OK)" && fixed.detail.contains("Contents are not verified"),
        "Fixed-size inspection changed status or claimed to validate arbitrary contents.");
    QFile preserved(kernal); require(preserved.open(QIODevice::ReadOnly) && preserved.readAll() == contents,
        "Inspection modified the resource file."); preserved.close();
    const auto first = QDir(banks).filePath("First.sf2"), second = QDir(banks).filePath("Second.SF2"), ignored = QDir(banks).filePath("Ignore.txt");
    write(first, {}); write(second, {}); write(ignored, {});
    const auto bankStatus = inspectPlayerResources()[6];
    require(bankStatus.directory && bankStatus.found && bankStatus.status == "2 found" && bankStatus.detail.contains("validated during playback"),
        "SoundFont presence no longer counts filenames independently of content validation.");
    for (const auto &path : {control, kernal, first, second, ignored}) require(QFile::remove(path), "Resource-check cleanup failed.");
    log << "Resource catalog: order/kinds, read-only presence, fixed/MT-32 size bounds and SoundFont count versus validation PASS\n"; log.flush();
}
}
void checkPlaybackBoundaries(QApplication &,const QDir &dir,QTextStream &log) {
    QTemporaryDir scratch(dir.filePath("boundary-checks-XXXXXX"));require(scratch.isValid(),"Boundary temporary directory failed.");
    for(bool pokey:{false,true})for(bool loop:{false,true}) {
        const auto path=scratch.filePath(QString("%1-%2.vgm").arg(pokey).arg(loop));write(path,fixture(pokey,loop));
        auto track=openMusicTrack(path);const auto boundary=track->boundary();
        require(track->info().durationMs<0 && boundary.frame==2*SampleRate && boundary.loopFrame==(loop?SampleRate:-1),"PSG/POKEY command boundary did not work without header time.");
        require(track->seek(1700*SampleRate/1000,[]{return false;}) && track->boundary().frame==boundary.frame,"Seeking lost the VGM boundary.");
    }
    QFile source(dir.filePath("intro-loop.vgm"));require(source.open(QIODevice::ReadOnly),"Yamaha fixture missing.");
    auto data=source.readAll();qToLittleEndian<quint32>(0,data.data()+0x18);qToLittleEndian<quint32>(0,data.data()+0x20);
    const auto yamaha=scratch.filePath("yamaha.vgm");write(yamaha,data);auto track=openMusicTrack(yamaha);
    require(track->info().durationMs<0 && track->boundary().frame==2*SampleRate && track->boundary().loopFrame==SampleRate,"Sega FM command boundary needs declared time.");
    QByteArray ym(0x100,'\0');ym.replace(0,4,"Vgm ");
    qToLittleEndian<quint32>(0x171,ym.data()+8);qToLittleEndian<quint32>(0x100-0x34,ym.data()+0x34);qToLittleEndian<quint32>(4000000,ym.data()+0x44);
    ym+=QByteArray::fromHex("55005055073e55080f6144ac6144ac66");
    qToLittleEndian<quint32>(ym.size()-4,ym.data()+4);qToLittleEndian<quint32>(0x100+12-0x1c,ym.data()+0x1c);
    const auto ymPath=scratch.filePath("ym2203.vgm");write(ymPath,ym);auto ymTrack=openMusicTrack(ymPath);
    require(ymTrack->info().durationMs<0 && ymTrack->boundary().frame==2*SampleRate && ymTrack->boundary().loopFrame==SampleRate,"Generic Yamaha boundary needs declared time.");
    auto module=openMusicTrack(dir.filePath("demo.mod"));require(module->boundary().frame<0,"Tracker reported a loop before playback.");
    std::array<short,BlockFrames*2> pcm{};
    while(module->boundary().frame<0 && module->frame()<60*SampleRate)module->render(pcm.data(),BlockFrames);
    require(module->boundary().frame>0 && module->boundary().frame<=module->frame(),"Tracker did not report its actual first loop.");
    module->seek(0,[]{return false;});require(module->boundary().frame<0,"Tracker seek retained an obsolete loop event.");
    auto nsf=openMusicTrack(dir.filePath("demo.nsf"));nsf->render(pcm.data(),BlockFrames);
    require(nsf->boundary().frame<0 && nsf->info().durationMs<0,"NSF invented a loop boundary.");
    log<<"Decoder end/loop boundaries without VGM duration metadata, tracker loop/reset and unknown NSF PASS\n";log.flush();
}
void checkPlayerSettings(QApplication &app,const QDir &dir,QTextStream &log) {
    QTemporaryDir scratch(dir.filePath("preferences-checks-XXXXXX"));require(scratch.isValid(),"Preference temporary directory failed.");
    const auto previousSettings=app.property("settingsFileForTests"),previousRoms=app.property("romsDirectoryForTests"),previousBanks=app.property("soundFontsDirectoryForTests");
    struct Restore {QApplication &app;QVariant settings,roms,banks;~Restore(){app.setProperty("settingsFileForTests",settings);app.setProperty("romsDirectoryForTests",roms);app.setProperty("soundFontsDirectoryForTests",banks);}} restore{app,previousSettings,previousRoms,previousBanks};
    app.setProperty("settingsFileForTests",scratch.filePath("settings.ini"));app.setProperty("romsDirectoryForTests",scratch.filePath("roms"));app.setProperty("soundFontsDirectoryForTests",scratch.filePath("soundfonts"));
    require(unknownTrackLimitPreference()==0,"Unknown duration default is not unlimited.");
    checkResourceInspection(QDir(scratch.path()),log);
    {
        PlayerWindow window;window.player().setVolume(0);window.show();auto &player=window.player();
        auto gear=window.findChild<QPushButton *>("playerSettingsButton");require(gear,"Settings gear missing.");gear->click();
        auto dialog=window.findChild<QDialog *>("playerSettings");require(dialog && !dialog->isModal(),"Settings is missing or modal.");
        window.showSettings();require(window.findChildren<QDialog *>("playerSettings").size()==1,"Settings duplicated its window.");
        auto tabs=dialog->findChild<QTabWidget *>("settingsTabs");require(tabs && tabs->count()==3 && tabs->tabText(0)=="Playback" && tabs->tabText(1)=="Rendering" && tabs->tabText(2)=="Resources","Settings tabs missing.");
        auto enabled=dialog->findChild<QCheckBox *>("unknownDurationEnabled");auto time=dialog->findChild<QTimeEdit *>("unknownDurationTime");
        require(!enabled->isChecked() && !time->isEnabled(),"Unlimited default is not reflected by controls.");
        window.loadFile(dir.filePath("demo.nsf"));spin(app,[&]{return !player.state().busy && player.state().valid;},"Unlimited NSF load failed.");
        require(player.state().playbackEndMs<0,"Unlimited NSF acquired an endpoint.");
        const auto generation=player.state().generation;
        time->setTime(QTime(0,0,1));enabled->setChecked(true);
        require(unknownTrackLimitPreference()==1000 && player.unknownDurationLimit()==1000 && time->isEnabled(),"Limit was not saved/applied.");
        require(player.state().generation==generation && player.state().playbackEndMs<0,"Changing limit interrupted the current track.");
        tabs->setCurrentIndex(1);auto fps=dialog->findChild<QComboBox *>("waveFrameRate");fps->setCurrentIndex(fps->findData(120));
        require(window.scopeWindow().frameRate()==120 && player.state().generation==generation,"FPS did not apply live or restarted audio.");
        QSettings settings(playerSettingsPath(),playerSettingsFormat());require(settings.value("Waveforms/FrameRate").toInt()==120,"FPS was not persisted.");
        tabs->setCurrentIndex(2);auto files=dialog->findChild<QTreeWidget *>("resourceFiles");
        require(files->topLevelItemCount()==7,"Resource list incomplete.");
        for(int i=0;i<7;++i)require(files->topLevelItem(i)->text(1)=="Missing","Resource checks escaped isolated folders.");
        QDir().mkpath(scratch.filePath("roms/c64"));write(scratch.filePath("roms/c64/kernal.rom"),QByteArray(7,'x'));
        auto refresh=dialog->findChild<QPushButton *>("refreshResources");refresh->click();require(files->topLevelItem(2)->text(1)=="Invalid size","Invalid ROM size went undetected.");
        write(scratch.filePath("roms/c64/kernal.rom"),QByteArray(8192,'x'));refresh->click();files->setCurrentItem(files->topLevelItem(2));
        require(files->topLevelItem(2)->text(1)=="Found (size OK)" && dialog->findChild<QLabel *>("resourceDetail")->text().contains("not verified"),"Presence check incorrectly claimed ROM validation.");
        require(player.state().generation==generation,"Resource refresh restarted playback.");
        dialog->resize(540,530);
        for(int i=0;i<3;++i){tabs->setCurrentIndex(i);app.processEvents();require(dialog->grab().save(dir.filePath(QString("settings-%1.png").arg(tabs->tabText(i).toLower()))),"Settings screenshot failed.");}
        require(window.grab().save(dir.filePath("settings-player.png")),"Gear screenshot failed.");
        // The new limit is captured by the next load, not by seek/pause/FPS changes.
        window.loadFile(dir.filePath("demo.nsf"));spin(app,[&]{return !player.state().busy && player.state().playbackEndMs==1000;},"Limit did not apply on next load.");
        window.setPlaylistRepeat(PlaylistRepeat::Off);player.seek(1000,false);spin(app,[&]{return !player.state().busy;},"Limit endpoint seek failed.");
        player.seek(player.state().positionMs,true);spin(app,[&]{return !player.state().busy && player.state().playing && player.state().positionMs<1000;},"Play after limited end did not restart.");
        player.seek(0,false);spin(app,[&]{return !player.state().busy;},"Limit pause failed.");
        const auto paused=player.state().generation;window.refresh();require(player.state().generation==paused && !player.state().finished,"Pause incorrectly advanced the queue.");
        window.loadFile(dir.filePath("named.nsfe"));spin(app,[&]{return !player.state().busy && player.state().info.path.endsWith("named.nsfe");},"Known track load failed.");
        require(player.state().playbackEndMs==4000 && !player.state().durationLimited,"Fallback limit truncated a known duration.");
        // First NSF subsong advances to second on the chosen limit.
        window.loadFile(dir.filePath("demo.nsf"));spin(app,[&]{return !player.state().busy && window.playlist().entries.size()==2;},"Limited playlist load failed.");
        spin(app,[&]{return !player.state().busy && player.state().info.song==1;},"Unknown-duration limit did not advance playlist.");
        window.setPlaylistRepeat(PlaylistRepeat::Track);const auto repeatGen=player.state().generation;player.seek(900,true);
        spin(app,[&]{return !player.state().busy && player.state().generation>repeatGen+1 && player.state().playing;},"Limited track repeat failed.");
        require(player.state().info.song==1,"Limited track repeat advanced playlist.");
        window.setPlaylistRepeat(PlaylistRepeat::All);player.seek(900,true);
        spin(app,[&]{return !player.state().busy && player.state().info.song==0;},"Limited playlist repeat failed.");
        window.setPlaylistRepeat(PlaylistRepeat::Off);
        // A command-stream boundary outranks the shorter fallback timeout.
        const auto loop=scratch.filePath("boundary.vgm");write(loop,fixture(false,true));window.loadFile(loop);
        spin(app,[&]{return !player.state().busy && player.state().info.path==loop;},"Boundary player load failed.");
        require(player.state().playbackEndMs==2000 && !player.state().durationLimited,"Timeout overrode a proven command boundary.");
        player.seek(1750,true);spin(app,[&]{return !player.state().busy;},"Boundary seek failed.");
        require(player.state().playing && !player.state().finished,"Read-ahead reported finish before audio consumption.");
        spin(app,[&]{return player.state().finished;},"Metadata-free VGM did not stop at first loop.");
        require(player.state().positionMs==2000 && !player.state().playing,"Loop end state is incorrect.");
        window.setPlaylistRepeat(PlaylistRepeat::Track);player.seek(1900,true);const auto loopGen=player.state().generation;
        spin(app,[&]{return !player.state().busy && player.state().generation>loopGen;},"Metadata-free VGM track repeat failed.");
        require(player.state().positionMs>=1000 && player.state().positionMs<1600,"VGM repeat replayed the intro or used wrong loop point.");
        enabled->setChecked(false);require(unknownTrackLimitPreference()==0,"Unlimited preference did not save.");
        window.close();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
        require(!window.findChild<QDialog *>("playerSettings"),"Settings survived closing the player.");
    }
    {PlayerWindow restored;require(restored.player().unknownDurationLimit()==0 && restored.scopeWindow().frameRate()==120,"Playback/FPS preferences did not survive restart.");restored.showSettings();auto d=restored.findChild<QDialog *>("playerSettings");require(d->findChild<QTimeEdit *>("unknownDurationTime")->time()==QTime(0,0,1),"Disabling limit forgot the selected duration.");restored.close();}
    log<<"Tabbed Settings, gear, live FPS, persistence, resource refresh, optional limits, end/repeat/playlist transitions and restart PASS\n";log.flush();
}
