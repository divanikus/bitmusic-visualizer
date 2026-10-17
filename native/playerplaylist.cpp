#include "playerwindow.h"
#include "playlistview.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
enum class PlaylistSymbol { Add, Remove, Menu };
class PlaylistButton final : public QToolButton {
public:
    PlaylistButton(PlaylistSymbol symbol,const char *name,const QString &tip) : symbol_(symbol) {
        setObjectName(name);setToolTip(tip);setAccessibleName(tip);
        setFixedSize(32,30);setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QColor fill(isEnabled()?"#202a30":"#20282d");
        if(underMouse() && isEnabled())fill=fill.lighter(118);
        if(isDown())fill=fill.darker(115);
        p.setPen(hasFocus()?QPen(QColor("#93d9cb"),1):QPen(Qt::NoPen));p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()).adjusted(.5,.5,-.5,-.5),6,6);
        p.translate((width()-20)/2.0,(height()-20)/2.0);
        p.setPen(QPen(QColor(isEnabled()?"#dbe5e5":"#647078"),1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        p.drawLine(QPointF(2,4),QPointF(13,4));
        p.drawLine(QPointF(2,9),QPointF(10,9));
        p.drawLine(QPointF(2,14),QPointF(7,14));
        if(symbol_==PlaylistSymbol::Menu) p.drawPolyline(QPolygonF{{12,12},{15,15},{18,12}});
        else {
            p.drawLine(QPointF(12,14),QPointF(18,14));
            if(symbol_==PlaylistSymbol::Add)p.drawLine(QPointF(15,11),QPointF(15,17));
        }
    }
private:
    PlaylistSymbol symbol_;
};
}
void PlayerWindow::setupPlaylistUi() {
    auto page=new QWidget;
    auto layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
    auto list = new PlaylistView; songs_ = list;
    list->onFiles=[this](const QStringList &paths){addFiles(paths);};
    list->onDelete=[this]{removePlaylistSelection();};
    layout->addWidget(songs_,1);
    auto actions=new QHBoxLayout;actions->setContentsMargins(2,0,2,0);actions->setSpacing(4);
    auto button=[&](PlaylistSymbol symbol,const char *name,const QString &tip) {
        auto b=new PlaylistButton(symbol,name,tip);actions->addWidget(b);return b;
    };
    auto add=button(PlaylistSymbol::Add,"addTracks","Add music files to playlist (Ctrl+Shift+O)");
    auto remove=button(PlaylistSymbol::Remove,"removeTracks","Remove selected tracks (Delete)");
    auto more=button(PlaylistSymbol::Menu,"playlistMenu","Playlist actions");
    more->setStyleSheet("QToolButton::menu-indicator { image: none; }");
    auto menu=new QMenu(more); more->setMenu(menu);more->setPopupMode(QToolButton::InstantPopup);
    menu->addAction("New playlist",this,[this]{clearPlaylist();});
    menu->addAction("Open playlist…",this,[this]{
        const auto path=QFileDialog::getOpenFileName(this,"Open playlist",{},"Playlists (*.m3u8 *.m3u)");
        if(!path.isEmpty()) addFiles({path},true);
    });
    menu->addAction("Save playlist…",this,[this]{
        auto path=QFileDialog::getSaveFileName(this,"Save playlist",playlistPath_.isEmpty()?"Playlist.m3u8":playlistPath_,"UTF-8 playlist (*.m3u8);;M3U playlist (*.m3u)");
        if(path.isEmpty())return;
        if(QFileInfo(path).suffix().isEmpty())path+=".m3u8";
        try {savePlaylist(path);} catch(const std::exception &e){QMessageBox::warning(this,"Could not save playlist",QString::fromUtf8(e.what()));}
    });
    playlistStatus_=new QLabel;playlistStatus_->setObjectName("playlistStatus");playlistStatus_->setStyleSheet("color: #92a6b0;");
    actions->addStretch();actions->addWidget(playlistStatus_);layout->addLayout(actions);
    tabs_->addTab(page,"Tracks");
    connect(add,&QToolButton::clicked,this,[this]{chooseMusic(false);});
    connect(remove,&QToolButton::clicked,this,[this]{removePlaylistSelection();});
    connect(songs_->model(),&QAbstractItemModel::rowsMoved,this,[this]{
        QVector<PlaylistEntry> reordered;
        for (auto id : songs_->entryIds()) { const int at = playlist_.row(id); if (at >= 0) reordered.push_back(playlist_.entries[at]); }
        if(reordered.size()==playlist_.entries.size()) {playlist_.entries=std::move(reordered);decoratePlaylist();}
    });
    auto addShortcut=new QShortcut(QKeySequence("Ctrl+Shift+O"),this);
    connect(addShortcut,&QShortcut::activated,this,[this]{chooseMusic(false);});
}
void PlayerWindow::chooseMusic(bool replace) {
    const QString trackers="*.mod mod.* *.m15 *.xm *.s3m *.it *.med med.* *.dbm *.digi *.emod *.mmd0 *.mmd1 *.mmd2 *.mmd3 *.mtm *.stm *.669 *.ult *.far *.okt *.umx";
    const auto filters=QStringList{
        QString("Music and playlists (*.nsf, *.vgm, ...) (*.nsf *.nsfe *.vgm *.vgz *.spc *.gbs *.ay *.sid *.mid *.midi *.ttt *.m3u *.m3u8 %1)").arg(trackers),
        "Playlists (*.m3u8 *.m3u)","NES (*.nsf *.nsfe)","VGM (*.vgm *.vgz)","SNES (*.spc)","Game Boy (*.gbs)",
        "ZX Spectrum (*.ay)","Commodore 64 (*.sid)","MIDI (*.mid *.midi)",
        QString("Trackers (*.mod, *.xm, ...) (%1)").arg(trackers),"Atari 2600 (*.ttt)","All files (*)"}.join(";;");
    const auto paths=QFileDialog::getOpenFileNames(this,replace?"Open music or playlist":"Add music to playlist",{},filters,nullptr,QFileDialog::HideNameFilterDetails);
    if(paths.isEmpty())return;
    if(replace && paths.size()==1 && !isPlaylistFile(paths.front())) loadFile(paths.front());
    else addFiles(paths,replace);
}
void PlayerWindow::addFiles(const QStringList &paths,bool replace) {
    if(paths.isEmpty())return;
    if(replace) {
        playlistLoader_.cancel();replacingPlaylist_=true;pendingSingle_=false;
        playlistPath_=paths.size()==1 && isPlaylistFile(paths.front())?paths.front():QString();
    }
    playlistMessage_.clear();playlistLoader_.add(paths);
}
void PlayerWindow::savePlaylist(const QString &path) {
    writePlaylistFile(path,playlist_.entries);playlistPath_=path;playlistMessage_="Playlist saved";
    QTimer::singleShot(2500,this,[this]{if(playlistMessage_=="Playlist saved")playlistMessage_.clear();});
}
void PlayerWindow::clearPlaylist() {
    playlistLoader_.cancel();playlist_.clear();pendingSingle_=replacingPlaylist_=false;playlistPath_.clear();playlistMessage_.clear();
    player_.unload();shownPath_.clear();shownSong_=-1;midiPath_.clear();midiControls_->hide();rebuildPlaylist();
}
void PlayerWindow::removePlaylistSelection() {
    const auto ids = songs_->selectedIds();
    if(ids.isEmpty())return;
    playlist_.remove(ids);rebuildPlaylist();
    if(playlist_.entries.isEmpty()) {player_.unload();midiControls_->hide();}
}
void PlayerWindow::rebuildPlaylist() {
    const QSignalBlocker block(songs_);
    songs_->rebuild(playlist_.entries);
    decoratePlaylist();
}
void PlayerWindow::decoratePlaylist() {
    songs_->decorate(playlist_.entries);
    tabs_->setTabText(0, QString("Tracks (%1)").arg(playlist_.entries.size()));
}
void PlayerWindow::setPlaylistRepeat(PlaylistRepeat mode) {
    playlistRepeat_=mode;player_.setRepeat(mode==PlaylistRepeat::Track);
}
void PlayerWindow::setPlaylistShuffle(bool enabled) {playlist_.setShuffle(enabled);}
void PlayerWindow::stepPlaylist(bool forward) {
    const int at=forward?playlist_.next(playlistRepeat_==PlaylistRepeat::All):playlist_.previous();
    if(at>=0)playSong(at);
}
void PlayerWindow::pollPlaylist() {
    for(auto &result:playlistLoader_.takeResults()) {
        if(!result.error.isEmpty()) {playlistMessage_=result.error;replacingPlaylist_=false;continue;}
        const bool start=replacingPlaylist_ || (playlist_.entries.isEmpty() && !pendingSingle_);
        if(replacingPlaylist_) {playlist_.clear();replacingPlaylist_=false;}
        try {playlist_.append(std::move(result.entries));}
        catch(const std::exception &e) {playlistMessage_=QString::fromUtf8(e.what());continue;}
        rebuildPlaylist();
        if(start && !playlist_.entries.isEmpty())playSong(0);
        else if(start)player_.unload();
    }
    auto s=player_.state();
    if(pendingSingle_ && !s.busy && s.generation==pendingSingleGeneration_) {
        pendingSingle_=false;
        if(s.valid && s.error.isEmpty() && s.info.path==pendingSinglePath_) {
            QVector<PlaylistEntry> entries;
            for(int i=0;i<s.info.playlist.size();++i) {const auto &song=s.info.playlist[i];entries.push_back({0,s.info.path,song.title,{},i,s.info.songs,song.durationMs});}
            // Appends may have completed while this single file was opening.
            auto appended=std::move(playlist_.entries);playlist_.clear();
            try {playlist_.append(std::move(entries));playlist_.append(std::move(appended));}
            catch(const std::exception &e) {playlistMessage_=QString::fromUtf8(e.what());}
            playlist_.activate(s.info.song);rebuildPlaylist();songs_->setCurrentRow(s.info.song);
        }
    }
    const int at=playlist_.currentRow();
    if(at>=0 && !s.busy) {
        auto &entry=playlist_.entries[at];
        if(s.valid && s.error.isEmpty() && entry.path==s.info.path && entry.song==s.info.song) {
            if(entry.durationMs!=s.info.durationMs || !entry.error.isEmpty()) {entry.durationMs=s.info.durationMs;entry.error.clear();decoratePlaylist();}
        } else if(!s.error.isEmpty() && entry.error!=s.error) {entry.error=s.error;decoratePlaylist();}
    }
    // finished is generation-scoped and cleared synchronously by every user command.
    if(s.finished && !s.busy && s.generation!=completedGeneration_) {
        completedGeneration_=s.generation;stepPlaylist(true);
    }
    playlistStatus_->setText(playlistLoader_.busy()?"Reading tracks…":QString());
}
