#include "playlistview.h"
#include "timeline.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <algorithm>

namespace {
constexpr int HeaderRole = PlaylistView::HeaderRole, SourceRole = PlaylistView::SourceRole;
constexpr int HeaderHeight = 23, TrackHeight = 30;
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
class TrackDelegate : public QStyledItemDelegate {
public:
    explicit TrackDelegate(QListWidget *list) : QStyledItemDelegate(list), list_(list) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override {
        return {180, !index.data(HeaderRole).toString().isEmpty()?HeaderHeight+TrackHeight:index.data(PlaylistView::SeparatorRole).toBool()?TrackHeight+9:TrackHeight};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing);
        const bool current = index.data(PlaylistView::CurrentRole).toBool();
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
        const bool separator=index.data(PlaylistView::SeparatorRole).toBool();
        if(separator) {p->setPen(QColor("#314047"));p->drawLine(option.rect.left()+10,option.rect.top()+4,option.rect.right()-10,option.rect.top()+4);}
        const auto r = option.rect.adjusted(10, header.isEmpty()?(separator?9:0):HeaderHeight, -10, 0);
        p->setFont(option.font); p->setPen(current ? QColor("#a2e5d5") : QColor("#8f9eaa"));
        if (index.data(PlaylistView::PlayingRole).toBool()) {
            p->setPen(Qt::NoPen); p->setBrush(QColor("#a2e5d5"));
            p->drawPolygon(QPolygonF{{qreal(r.left() + 4), qreal(r.center().y() - 4)},
                {qreal(r.left() + 11), qreal(r.center().y())}, {qreal(r.left() + 4), qreal(r.center().y() + 4)}});
        } else p->drawText(QRect(r.left(), r.top(), 25, r.height()), Qt::AlignVCenter, QString::number(index.row() + 1));
        p->setPen(index.data(PlaylistView::ErrorRole).toBool()?QColor("#efb49a"):current ? QColor("#c3f1e7") : QColor("#dce5e8"));
        const QRect title(r.left() + 28, r.top(), std::max(0, r.width() - 86), r.height());
        p->drawText(title, Qt::AlignVCenter, option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, title.width()));
        p->setPen(QColor("#92a6b0"));
        p->drawText(QRect(r.right() - 52, r.top(), 52, r.height()), Qt::AlignRight | Qt::AlignVCenter, index.data(PlaylistView::DurationRole).toString());
        p->restore();
    }
private:
    QListWidget *list_;
};
}
PlaylistView::PlaylistView(QWidget *parent) : QListWidget(parent) {
    setObjectName("songs"); setMouseTracking(true);
    setItemDelegate(new TrackDelegate(this));
    connect(this, &QListWidget::itemSelectionChanged, viewport(), QOverload<>::of(&QWidget::update));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragDropMode(QAbstractItemView::InternalMove); setDefaultDropAction(Qt::MoveAction);
    setDragDropOverwriteMode(false); setDropIndicatorShown(true);
}
void PlaylistView::mousePressEvent(QMouseEvent *event) {
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
void PlaylistView::mouseReleaseEvent(QMouseEvent *event) {
    if(headerPressed_ && event->button()==Qt::LeftButton) {
        // A normal selected-row release would collapse the group to its first song.
        headerPressed_=false;setState(NoState);event->accept();
    } else QListWidget::mouseReleaseEvent(event);
}
void PlaylistView::mouseDoubleClickEvent(QMouseEvent *event) {
    if(headerAt(event)>=0) {headerPressed_=true;event->accept();}
    else QListWidget::mouseDoubleClickEvent(event);
}
void PlaylistView::dragEnterEvent(QDragEnterEvent *event) {
    if(!localFiles(event->mimeData()).isEmpty()) event->acceptProposedAction(); else QListWidget::dragEnterEvent(event);
}
void PlaylistView::dragMoveEvent(QDragMoveEvent *event) {
    if(!localFiles(event->mimeData()).isEmpty()) event->acceptProposedAction(); else QListWidget::dragMoveEvent(event);
}
void PlaylistView::dropEvent(QDropEvent *event) {
    const auto paths=localFiles(event->mimeData());
    if(!paths.isEmpty()) {if (onFiles) onFiles(paths);event->acceptProposedAction();} else QListWidget::dropEvent(event);
}
void PlaylistView::keyPressEvent(QKeyEvent *event) {
    if(event->key()==Qt::Key_Delete && event->modifiers()==Qt::NoModifier) {if (onDelete) onDelete();event->accept();}
    else {
        QListWidget::keyPressEvent(event);
        if(!(event->modifiers() & Qt::ShiftModifier))selectionAnchor_=currentIndex();
    }
}
int PlaylistView::headerAt(const QMouseEvent *event) const {
    if(event->button()!=Qt::LeftButton)return -1;
    auto item=itemAt(event->position().toPoint());
    if(!item || item->data(HeaderRole).toString().isEmpty())return -1;
    return event->position().y()<visualItemRect(item).top()+HeaderHeight?row(item):-1;
}

void PlaylistView::rebuild(const QVector<PlaylistEntry> &entries) {
    const auto selected = selectedIds();
    const auto selectedId = currentItem() ? currentItem()->data(IdRole).toULongLong() : 0;
    const QSignalBlocker block(this);
    clear();
    for (const auto &entry : entries) {
        auto item = new QListWidgetItem(entry.title, this);
        item->setData(IdRole, QVariant::fromValue(entry.id));
        if (entry.id == selectedId) setCurrentItem(item, QItemSelectionModel::NoUpdate);
        item->setSelected(selected.contains(entry.id));
    }
}
void PlaylistView::decorate(const QVector<PlaylistEntry> &entries) {
    for(int i=0;i<entries.size();++i) {
        const auto &entry=entries[i];auto item=this->item(i);if(!item)continue;
        item->setText(entry.title);item->setData(DurationRole,entry.durationMs>0?timeText(entry.durationMs):QString::fromUtf8("—"));
        item->setData(SourceRole,entry.path);
        item->setData(HeaderRole,entry.songs>1 && (i==0 || entries[i-1].path!=entry.path)?QFileInfo(entry.path).fileName():QString());
        item->setData(SeparatorRole,entry.songs==1 && i>0 && entries[i-1].songs>1 && entries[i-1].path!=entry.path);
        item->setData(ErrorRole,!entry.error.isEmpty());
        item->setToolTip(entry.title+"\n"+entry.path+(entry.songs>1?QString("\nSubsong %1 of %2").arg(entry.song+1).arg(entry.songs):QString())+
                         (entry.error.isEmpty()?QString():"\n"+entry.error));
    }
}
void PlaylistView::presentPlayback(quint64 current, bool playing) {
    for (int i = 0; i < count(); ++i) {
        auto entry = item(i);
        const bool active = current != 0 && entry->data(IdRole).toULongLong() == current;
        if (entry->data(CurrentRole).toBool() != active) entry->setData(CurrentRole, active);
        const bool audible = active && playing;
        if (entry->data(PlayingRole).toBool() != audible) entry->setData(PlayingRole, audible);
    }
}
QVector<quint64> PlaylistView::entryIds() const {
    QVector<quint64> result;
    for (int i = 0; i < count(); ++i) result.push_back(item(i)->data(IdRole).toULongLong());
    return result;
}
QSet<quint64> PlaylistView::selectedIds() const {
    QSet<quint64> result;
    for (auto entry : selectedItems()) result.insert(entry->data(IdRole).toULongLong());
    return result;
}
