#pragma once
#include "playlist.h"
#include <QListWidget>
#include <QPersistentModelIndex>
#include <functional>

// Playlist presentation and selection only. Entries, import and playback remain
// owned by the player/model; drawn source headers are never separate rows.
class PlaylistView : public QListWidget {
public:
    enum Role {
        DurationRole = Qt::UserRole, CurrentRole, PlayingRole, HeaderRole,
        IdRole, ErrorRole, SeparatorRole, SourceRole
    };
    explicit PlaylistView(QWidget *parent = nullptr);
    void rebuild(const QVector<PlaylistEntry> &entries);
    void decorate(const QVector<PlaylistEntry> &entries);
    void presentPlayback(quint64 current, bool playing);
    QVector<quint64> entryIds() const;
    QSet<quint64> selectedIds() const;
    std::function<void(const QStringList &)> onFiles;
    std::function<void()> onDelete;
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    int headerAt(const QMouseEvent *event) const;
    bool headerPressed_ = false;
    QPersistentModelIndex selectionAnchor_;
};
