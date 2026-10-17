#include "window.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int HeaderRole=Qt::UserRole+3, IdRole=Qt::UserRole+4, SourceRole=Qt::UserRole+7;
constexpr int HeaderHeight=23, TrackHeight=30;
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

int groupEnd(const QListWidget &list,int first) {
    const auto source=list.item(first)->data(SourceRole).toString();
    int end=first+1;
    while(end<list.count() && list.item(end)->data(SourceRole).toString()==source)++end;
    return end;
}
bool groupSelected(const QListWidget &list,int first,int end) {
    for(int i=first;i<end;++i)if(!list.item(i)->isSelected())return false;
    return true;
}
QStringList localFiles(const QMimeData *mime) {
    QStringList paths; for(const auto &url:mime->urls()) if(url.isLocalFile()) paths<<url.toLocalFile(); return paths;
}
class PlaylistList : public QListWidget {
public:
    std::function<void(const QStringList&)> onFiles;
    std::function<void()> onDelete;
protected:
    void mousePressEvent(QMouseEvent *event) override {
        int first=headerAt(event);
        headerPressed_=first>=0;
        int end=headerPressed_?groupEnd(*this,first):0;
        if(event->button()==Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier))
            selectionAnchor_=indexAt(event->position().toPoint());
        const int anchor=selectionAnchor_.isValid()?selectionAnchor_.row():currentRow();
        const bool selected=headerPressed_ && groupSelected(*this,first,end);
        // Let Qt initialize focus, the selection anchor and native drag handling.
        QListWidget::mousePressEvent(event);
        if(!headerPressed_)return;
        auto command=QItemSelectionModel::ClearAndSelect;
        if(event->modifiers() & Qt::ShiftModifier) {
            if(anchor>=0) {first=std::min(first,anchor);end=std::max(end,anchor+1);}
            if(event->modifiers() & Qt::ControlModifier)command=QItemSelectionModel::Select;
        } else if(event->modifiers() & Qt::ControlModifier)command=selected?QItemSelectionModel::Deselect:QItemSelectionModel::Select;
        selectionModel()->select(QItemSelection(model()->index(first,0),model()->index(end-1,0)),command);
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        if(headerPressed_ && event->button()==Qt::LeftButton) {
            // A normal selected-row release would collapse the group to its first song.
            headerPressed_=false;setState(NoState);event->accept();
        } else QListWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if(headerAt(event)>=0) {headerPressed_=true;event->accept();}
        else QListWidget::mouseDoubleClickEvent(event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if(!localFiles(event->mimeData()).isEmpty()) event->acceptProposedAction(); else QListWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if(!localFiles(event->mimeData()).isEmpty()) event->acceptProposedAction(); else QListWidget::dragMoveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        const auto paths=localFiles(event->mimeData());
        if(!paths.isEmpty()) {onFiles(paths);event->acceptProposedAction();} else QListWidget::dropEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if(event->key()==Qt::Key_Delete && event->modifiers()==Qt::NoModifier) {onDelete();event->accept();}
        else {
            QListWidget::keyPressEvent(event);
            if(!(event->modifiers() & Qt::ShiftModifier))selectionAnchor_=currentIndex();
        }
    }
private:
    bool headerPressed_=false;
    QPersistentModelIndex selectionAnchor_;
    int headerAt(const QMouseEvent *event) const {
        if(event->button()!=Qt::LeftButton)return -1;
        auto item=itemAt(event->position().toPoint());
        if(!item || item->data(HeaderRole).toString().isEmpty())return -1;
        return event->position().y()<visualItemRect(item).top()+HeaderHeight?row(item):-1;
    }
};
class TrackDelegate : public QStyledItemDelegate {
public:
    explicit TrackDelegate(QListWidget *list) : QStyledItemDelegate(list), list_(list) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        return {180, !index.data(HeaderRole).toString().isEmpty()?HeaderHeight+TrackHeight:index.data(Qt::UserRole+6).toBool()?TrackHeight+9:TrackHeight};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        const bool current = index.data(Qt::UserRole + 1).toBool();
        const auto header=index.data(HeaderRole).toString();
        const auto songRect=option.rect.adjusted(0,header.isEmpty()?0:HeaderHeight,0,0);
        if (option.state & QStyle::State_Selected) p->fillRect(songRect, QColor("#263f3f"));
        else if (option.state & QStyle::State_MouseOver) p->fillRect(songRect, QColor("#222f35"));
        if(!header.isEmpty()) {
            const QRect headerRect(option.rect.topLeft(),QSize(option.rect.width(),HeaderHeight));
            if(groupSelected(*list_,index.row(),groupEnd(*list_,index.row())))p->fillRect(headerRect,QColor("#263f3f"));
            else if(option.state & QStyle::State_MouseOver)p->fillRect(headerRect,QColor("#222f35"));
            const auto hr=option.rect.adjusted(10,0,-10,0);p->setPen(QColor("#92a6b0"));p->setFont(option.font);
            p->drawText(QRect(hr.left(),hr.top(),hr.width(),HeaderHeight-1),Qt::AlignVCenter,option.fontMetrics.elidedText(header,Qt::ElideMiddle,hr.width()));
            p->setPen(QColor("#314047"));p->drawLine(hr.left(),hr.top()+HeaderHeight-1,hr.right(),hr.top()+HeaderHeight-1);
        }
        const bool separator=index.data(Qt::UserRole+6).toBool();
        if(separator) {p->setPen(QColor("#314047"));p->drawLine(option.rect.left()+10,option.rect.top()+4,option.rect.right()-10,option.rect.top()+4);}
        const auto r = option.rect.adjusted(10, header.isEmpty()?(separator?9:0):HeaderHeight, -10, 0);
        p->setFont(option.font); p->setPen(current ? QColor("#a2e5d5") : QColor("#8f9eaa"));
        if (index.data(Qt::UserRole + 2).toBool()) {
            p->setPen(Qt::NoPen); p->setBrush(QColor("#a2e5d5"));
            p->drawPolygon(QPolygonF{{qreal(r.left() + 4), qreal(r.center().y() - 4)},
                {qreal(r.left() + 11), qreal(r.center().y())}, {qreal(r.left() + 4), qreal(r.center().y() + 4)}});
        } else p->drawText(QRect(r.left(), r.top(), 25, r.height()), Qt::AlignVCenter, QString::number(index.row() + 1));
        p->setPen(index.data(Qt::UserRole+5).toBool()?QColor("#efb49a"):current ? QColor("#c3f1e7") : QColor("#dce5e8"));
        const QRect title(r.left() + 28, r.top(), std::max(0, r.width() - 86), r.height());
        p->drawText(title, Qt::AlignVCenter, option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, title.width()));
        p->setPen(QColor("#92a6b0"));
        p->drawText(QRect(r.right() - 52, r.top(), 52, r.height()), Qt::AlignRight | Qt::AlignVCenter, index.data(Qt::UserRole).toString());
        p->restore();
    }
private:
    QListWidget *list_;
};
}
void PlayerWindow::setupPlaylistUi() {
    auto page=new QWidget;
    auto layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
    auto list=new PlaylistList; songs_=list;
    songs_->setItemDelegate(new TrackDelegate(songs_));
    connect(songs_,&QListWidget::itemSelectionChanged,songs_->viewport(),QOverload<>::of(&QWidget::update));
    songs_->setObjectName("songs");songs_->setMouseTracking(true);songs_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    songs_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    songs_->setDragDropMode(QAbstractItemView::InternalMove);songs_->setDefaultDropAction(Qt::MoveAction);
    songs_->setDragDropOverwriteMode(false);songs_->setDropIndicatorShown(true);
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
        for(int i=0;i<songs_->count();++i) {const int at=playlist_.row(songs_->item(i)->data(IdRole).toULongLong());if(at>=0)reordered.push_back(playlist_.entries[at]);}
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
    QSet<quint64> ids;for(auto item:songs_->selectedItems())ids.insert(item->data(IdRole).toULongLong());
    if(ids.isEmpty())return;
    playlist_.remove(ids);rebuildPlaylist();
    if(playlist_.entries.isEmpty()) {player_.unload();midiControls_->hide();}
}
void PlayerWindow::rebuildPlaylist() {
    QSet<quint64> selected;for(auto item:songs_->selectedItems())selected.insert(item->data(IdRole).toULongLong());
    const auto selectedId=songs_->currentItem()?songs_->currentItem()->data(IdRole).toULongLong():0;
    const QSignalBlocker block(songs_);songs_->clear();
    for(const auto &entry:playlist_.entries) {
        auto item=new QListWidgetItem(entry.title,songs_);item->setData(IdRole,QVariant::fromValue(entry.id));
        if(entry.id==selectedId)songs_->setCurrentItem(item,QItemSelectionModel::NoUpdate);
        item->setSelected(selected.contains(entry.id));
    }
    decoratePlaylist();
}
void PlayerWindow::decoratePlaylist() {
    for(int i=0;i<playlist_.entries.size();++i) {
        const auto &entry=playlist_.entries[i];auto item=songs_->item(i);if(!item)continue;
        item->setText(entry.title);item->setData(Qt::UserRole,entry.durationMs>0?timeText(entry.durationMs):QString::fromUtf8("—"));
        item->setData(SourceRole,entry.path);
        item->setData(HeaderRole,entry.songs>1 && (i==0 || playlist_.entries[i-1].path!=entry.path)?QFileInfo(entry.path).fileName():QString());
        item->setData(Qt::UserRole+6,entry.songs==1 && i>0 && playlist_.entries[i-1].songs>1 && playlist_.entries[i-1].path!=entry.path);
        item->setData(Qt::UserRole+5,!entry.error.isEmpty());
        item->setToolTip(entry.title+"\n"+entry.path+(entry.songs>1?QString("\nSubsong %1 of %2").arg(entry.song+1).arg(entry.songs):QString())+
                         (entry.error.isEmpty()?QString():"\n"+entry.error));
    }
    tabs_->setTabText(0,QString("Tracks (%1)").arg(playlist_.entries.size()));
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
