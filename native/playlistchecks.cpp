#include "playlist.h"
#include "window.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QToolButton>
#include <stdexcept>

namespace {
void require(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
void spin(QApplication &app,const std::function<bool()> &ready,const char *message,int limit=12000) {
    QElapsedTimer timer;timer.start();while(!ready() && timer.elapsed()<limit){app.processEvents();QThread::msleep(5);}require(ready(),message);
}
void write(const QString &path,const QByteArray &data) {QFile f(path);require(f.open(QIODevice::WriteOnly) && f.write(data)==data.size(),"Fixture write failed.");}
}
int runPlaylistChecks(QApplication &app,const QStringList &args) {
    QTextStream log(stdout);
    try {
        const int at=args.indexOf("--playlist-checks");require(at+1<args.size(),"Supply fixture directory.");
        const QDir dir{QDir{args[at+1]}.absolutePath()};QTemporaryDir scratch(dir.filePath("playlist-checks-XXXXXX"));require(scratch.isValid(),"Temporary directory failed.");
        const QString named=dir.filePath("named.nsfe"), loop=dir.filePath("intro-loop.vgm");
        app.setProperty("settingsFileForTests",scratch.filePath("settings.ini"));
        Playlist model;
        model.append({{0,named,"A",{},0,3,2000},{0,loop,"B",{},0,1,2000},{0,named,"C",{},2,3,2000}});
        model.activate(0);const auto first=model.current;std::swap(model.entries[0],model.entries[2]);
        require(model.current==first && model.currentRow()==2 && model.next(false)<0,"Moving current track changed its identity/end.");
        require(model.next(true)==0,"Repeat playlist did not wrap.");
        model.setShuffle(true);QSet<quint64> heard{model.current};
        for(int i=0;i<2;++i){require(model.next(false)>=0 && !heard.contains(model.current),"Shuffle repeated before visiting every track.");heard.insert(model.current);}
        require(!model.hasNext(false) && model.next(false)<0,"Shuffle did not stop at end.");
        auto last=model.current;require(model.previous()>=0 && model.current!=last,"Shuffle Previous lost history.");
        require(model.next(false)>=0 && model.current==last,"Shuffle Next did not follow forward history.");
        require(model.next(true)>=0 && model.current!=last,"Shuffle repeat immediately repeated boundary track.");
        model.setShuffle(false);model.activate(0);const auto successor=model.entries[1].id;model.remove({model.current});
        require(model.next(false)>=0 && model.current==successor,"Removing playing entry lost its successor.");
        model.activate(model.entries.size()-1);model.remove({model.current});
        require(!model.hasNext(false) && model.next(false)<0,"Removing final playing entry restarted queue.");
        model.clear();model.append({{0,named,"A",{},0},{0,named,"B",{},1},{0,named,"C",{},2}});model.activate(1);
        model.remove({model.current});model.remove({model.entries.last().id});
        require(model.next(false)<0,"Removing detached successor unexpectedly restarted queue.");
        model.append({{0,loop,"New",{},0}});require(model.next(false)==1,"Append after deleting playing tail lost its successor.");
        model.remove({model.current});require(model.previous()==0,"Previous after removing playing tail lost its predecessor.");

        QVector<PlaylistEntry> saved{{0,named,QString::fromUtf8("Тема · Intro"),{},2,3,2000},{0,loop,"Loop",{},0,1,-1},{0,named,"Again",{},1,3,-1}};
        const auto m3u=scratch.filePath("mix.m3u8");writePlaylistFile(m3u,saved);const auto round=readPlaylistFile(m3u);
        require(round.size()==3 && round[0].path==named && round[0].song==2 && round[0].title==saved[0].title && round[2].song==1,"M3U8 Unicode/subsong/order round trip failed.");
        const auto unicode=scratch.filePath(QString::fromUtf8("Музыка #1.nsfe"));require(QFile::copy(named,unicode),"Unicode fixture copy failed.");
        const auto unicodeList=scratch.filePath("unicode.m3u8");writePlaylistFile(unicodeList,{{0,unicode,"Unicode",{},1}});
        require(readPlaylistFile(unicodeList)[0].path==unicode,"Unicode filename round trip failed.");
        bool musicProtected=false;try{writePlaylistFile(unicode,saved);}catch(...){musicProtected=true;}require(musicProtected,"Playlist export accepted a music extension.");
        write(scratch.filePath("plain.m3u"),QByteArray("#EXTM3U\r\n# ignored\r\n")+QDir::toNativeSeparators(named).toUtf8()+"\r\n");
        require(readPlaylistFile(scratch.filePath("plain.m3u"))[0].song==-1,"Plain M3U did not request subsong expansion.");
        write(scratch.filePath("bad.m3u8"),"#EXTBMV-SUBSONG:0\na.nsf\n");bool rejected=false;
        try{readPlaylistFile(scratch.filePath("bad.m3u8"));}catch(...){rejected=true;}require(rejected,"Invalid subsong accepted.");
        write(scratch.filePath("network.m3u8"),"https://example.invalid/a.nsf\n");rejected=false;
        try{readPlaylistFile(scratch.filePath("network.m3u8"));}catch(...){rejected=true;}require(rejected,"Nonlocal playlist input accepted.");
        PlaylistLoader loader;loader.add({m3u});QVector<PlaylistLoader::Result> result;
        spin(app,[&]{result+=loader.takeResults();return !result.isEmpty();},"Playlist metadata read timed out.");
        require(result[0].error.isEmpty() && result[0].entries.size()==3 && result[0].entries[0].song==2 && result[0].entries[0].songs==3,"Subsong import expanded or lost selection.");
        loader.add({named});loader.cancel();loader.add({loop});result.clear();
        spin(app,[&]{result+=loader.takeResults();return !result.isEmpty();},"Cancelled loader did not recover.");
        require(result.size()==1 && result[0].entries.size()==1 && result[0].entries[0].path==loop,"Cancelled metadata leaked into newer queue.");
        loader.add({scratch.filePath("missing.nsf")});result.clear();
        spin(app,[&]{result+=loader.takeResults();return !result.isEmpty();},"Missing-file scan timed out.");
        require(result[0].entries.size()==1 && !result[0].entries[0].error.isEmpty(),"Missing entry was silently lost.");
        log<<"Playlist order, identity, shuffle history/cycles, removal, M3U8, metadata and cancellation PASS\n";log.flush();
        if(args.contains("--decoder-only"))return 0;

        PlayerWindow window;window.player().setVolume(0);window.show();
        auto &player=window.player();auto list=window.findChild<QListWidget*>("songs");
        window.loadFile(named);
        spin(app,[&]{return !player.state().busy && window.playlist().entries.size()==3;},"Single-file expansion failed.");
        player.seek(100,false);spin(app,[&]{return !player.state().busy;},"Pause failed.");
        const auto generation=player.state().generation;const auto identity=window.playlist().current;
        window.addFiles({loop});
        spin(app,[&]{return window.playlist().entries.size()==4;},"Append failed.");
        require(player.state().generation==generation && window.playlist().current==identity,"Append interrupted playback.");
        require(!list->item(0)->data(Qt::UserRole+3).toString().isEmpty() && list->item(1)->data(Qt::UserRole+3).toString().isEmpty(),"Subsong headers are not contiguous groups.");
        require(list->model()->moveRows({},0,1,{},3),"List drag move failed.");window.refresh();
        require(window.playlist().current==identity && window.playlist().currentRow()==2 && player.state().generation==generation,"Reorder changed the playing entry.");
        list->clearSelection();list->setCurrentRow(0);window.findChild<QToolButton*>("removeTracks")->click();
        require(window.playlist().entries.size()==3 && player.state().generation==generation,"Remove interrupted playback or removed wrong number.");
        const auto savePath=scratch.filePath("edited.m3u8");window.savePlaylist(savePath);
        const auto exported=readPlaylistFile(savePath);require(exported.size()==3 && exported[1].song==0,"Edited playlist did not retain subsong order.");
        window.findChild<QPushButton*>("next")->click();
        spin(app,[&]{return !player.state().busy && player.state().info.path==loop;},"Next did not cross source files.");
        window.setPlaylistRepeat(PlaylistRepeat::Off);player.seek(1850,true);
        spin(app,[&]{return !player.state().busy && !player.state().playing && player.state().finished;},"Last queue item did not finish.");
        window.setPlaylistRepeat(PlaylistRepeat::All);player.seek(1850,true);
        spin(app,[&]{return !player.state().busy && player.state().playing && player.state().info.path==named;},"Repeat playlist did not advance to first remaining subsong.");
        require(player.state().info.song==2,"Repeat ignored edited subsong order.");
        window.setPlaylistRepeat(PlaylistRepeat::Off);player.seek(1850,true);
        spin(app,[&]{return !player.state().busy && player.state().info.song==0;},"Natural transition followed decoder's subsong order instead of playlist.");
        window.setPlaylistRepeat(PlaylistRepeat::Track);player.seek(player.state().info.durationMs-100,true);
        const auto trackGen=player.state().generation;
        spin(app,[&]{return !player.state().busy && player.state().generation!=trackGen && player.state().playing;},"Track repeat failed.");
        require(player.state().info.path==named && player.state().info.song==0,"Track repeat advanced queue.");
        window.setPlaylistRepeat(PlaylistRepeat::Off);player.seek(0,false);spin(app,[&]{return !player.state().busy;},"Stop failed.");
        const auto stopGen=player.state().generation;window.refresh();require(player.state().generation==stopGen && !player.state().playing,"Stop caused automatic advancement.");
        window.loadFile(dir.filePath("demo.nsf"));
        spin(app,[&]{return !player.state().busy && player.state().info.path.endsWith("demo.nsf") && window.playlist().entries.size()>0;},"Unknown-duration file load failed.");
        window.setPlaylistRepeat(PlaylistRepeat::All);player.seek(181000,true);
        spin(app,[&]{return !player.state().busy && player.state().positionMs>181000;},"Unknown-duration playback stopped.");require(!player.state().finished,"Unknown duration invented an endpoint.");
        window.addFiles({savePath},true);
        spin(app,[&]{return !player.state().busy && window.playlist().entries.size()==3 && player.state().info.path==named && player.state().info.song==2;},"Saved playlist failed to reopen.");
        player.seek(0,false);spin(app,[&]{return !player.state().busy;},"Screenshot pause failed.");window.refresh();
        window.resize(430,480);app.processEvents();require(window.grab().save(dir.filePath("playlist.png")),"Playlist screenshot failed.");
        // Editing a running track never identifies it by its row number.
        window.setPlaylistRepeat(PlaylistRepeat::Off);window.loadFile(named);
        spin(app,[&]{return !player.state().busy && window.playlist().entries.size()==3;},"Active-edit fixture failed.");
        const auto running=player.state().generation;window.addFiles({loop});
        spin(app,[&]{return window.playlist().entries.size()==4;},"Running append failed.");
        require(player.state().playing && player.state().generation==running,"Running append restarted audio.");
        require(list->model()->moveRows({},0,2,{},4),"Multiple-row move failed.");
        require(window.playlist().currentRow()==2 && player.state().generation==running,"Multiple-row move lost active track.");
        list->clearSelection();list->setCurrentRow(2);
        window.findChild<QToolButton*>("removeTracks")->click();
        require(player.state().generation==running && player.state().playing,"Removing the running row interrupted playback.");
        player.seek(player.state().info.durationMs-100,true);
        spin(app,[&]{return !player.state().busy && player.state().info.song==1;},"Deleted playing track did not advance to its successor.");
        list->selectAll();QKeyEvent deletion(QEvent::KeyPress,Qt::Key_Delete,Qt::NoModifier);QApplication::sendEvent(list,&deletion);
        spin(app,[&]{return !player.state().busy && !player.state().valid;},"Deleting the last entries did not unload audio.");
        require(window.playlist().entries.isEmpty(),"Delete did not remove all selected rows.");
        window.loadFile(named);spin(app,[&]{return !player.state().busy && window.playlist().entries.size()==3;},"Command-priority fixture failed.");
        player.seek(player.state().info.durationMs-80,true);spin(app,[&]{return !player.state().busy;},"Endpoint seek failed.");
        // Hold GUI polling while the audio worker reaches EOF, then send a newer user command.
        QElapsedTimer endpoint;endpoint.start();while(!player.state().finished && endpoint.elapsed()<2000)QThread::msleep(5);
        require(player.state().finished,"Natural EOF was not reported.");window.findChild<QPushButton*>("next")->click();window.refresh();
        spin(app,[&]{return !player.state().busy;},"User Next at EOF failed.");require(player.state().info.song==1,"A stale natural-end event overrode user Next.");
        window.setPlaylistShuffle(true);window.setPlaylistRepeat(PlaylistRepeat::All);window.close();
        {PlayerWindow restored;require(restored.playlist().shuffle,"Shuffle preference was not saved.");require(restored.findChild<QPushButton*>("repeat")->toolTip().contains("Playlist"),"Repeat preference was not saved.");}
        log<<"Live append/reorder/remove, file/subsong transitions, repeat modes, unknown duration, persistence and UI PASS\n";return 0;
    } catch(const std::exception &e) {log<<"FAIL: "<<e.what()<<'\n';return 1;}
}
