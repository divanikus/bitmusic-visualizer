#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <algorithm>

QRectF ScopeWindow::colorButtonRect(const QRectF &card) const {
    return card.width() >= 82 && card.height() >= 28 ? QRectF(card.right()-56, card.top()+2, 24, 24) : QRectF();
}
QRectF ScopeWindow::cardRect(int slot) const {
    const int columns = columns_->value(), rows = rows_->value();
    const int top = edit_->isChecked() ? channelPanel_->y() : 44;
    const qreal right = edit_->isChecked() ? 226 : 14;
    const qreal cellWidth = std::max(1.0, (width()-14.0-right-(columns-1)*10)/columns);
    const qreal bottom = height()-14;
    const qreal gap = std::clamp((bottom-top-rows)/std::max(1, rows-1), 0., 10.);
    const qreal cellHeight = std::max(1.0, (bottom-top-(rows-1)*gap)/rows);
    return {14+(slot%columns)*(cellWidth+10), top+(slot/columns)*(cellHeight+gap), cellWidth, cellHeight};
}
QRectF ScopeWindow::cellsRect(const QRect &cells) const {
    if (cells.isEmpty()) return {};
    return cardRect(cells.y()*columns_->value()+cells.x()).united(
        cardRect(cells.bottom()*columns_->value()+cells.right()));
}
int ScopeWindow::slotAt(const QPointF &point) const {
    if (!cellsRect(QRect(0, 0, columns_->value(), rows_->value())).contains(point)) return -1;
    const auto first = cardRect(0);
    const qreal pitchY = rows_->value() > 1 ? cardRect(columns_->value()).top()-first.top() : first.height();
    const int x = std::min(columns_->value()-1, int((point.x()-first.left())/(first.width()+10)));
    const int y = std::min(rows_->value()-1, int((point.y()-first.top())/pitchY));
    return y*columns_->value()+x;
}
const ScopeGrid &ScopeWindow::displayLayout() const {
    return (dragging_ || resizeChannel_ >= 0) && previewValid_ ? previewLayout_ : gridLayout_;
}
QVector<int> ScopeWindow::displayedChannels() const {
    QVector<int> result;
    for (int channel : order_) if (displayLayout().contains(channel)) result.push_back(channel);
    return result;
}
QRectF ScopeWindow::panelRect(int channel) const {
    if (dragging_ && channel == pressedChannel_) return dragCard_.translated(dragPosition_-dragStart_);
    if (!displayLayout().contains(channel)) return {};
    if (animationFrom_.contains(channel) && animationTo_.contains(channel)) {
        const qreal remaining = 1.0-std::clamp(motionClock_.elapsed()/160.0, 0.0, 1.0);
        const qreal progress = 1.0-remaining*remaining*remaining;
        const auto from = animationFrom_[channel], to = animationTo_[channel];
        return {from.topLeft()+(to.topLeft()-from.topLeft())*progress, from.size()+(to.size()-from.size())*progress};
    }
    return cellsRect(displayLayout().value(channel));
}
int ScopeWindow::panelAt(const QPointF &point) const {
    for (int channel : displayedChannels()) if (panelRect(channel).contains(point)) return channel;
    return -1;
}
int ScopeWindow::resizeEdgesAt(const QRectF &card, const QPointF &point) const {
    // Keep the header buttons separate and all hit regions inside the card,
    // away from the frameless window's own resize border.
    if (card.width() < 30 || card.height() < 42 || !card.contains(point) || point.y() < card.top()+28) return 0;
    const bool corner = point.x() >= card.right()-16 && point.y() >= card.bottom()-16;
    return (corner || point.x() >= card.right()-7 ? 1 : 0) | (corner || point.y() >= card.bottom()-7 ? 2 : 0);
}
QVector<int> ScopeWindow::dropOrder() const {
    auto result = order_;
    if (pressedChannel_ < 0 || dropSlot_ < 0) return result;
    const QPoint cell(dropSlot_%columns_->value(), dropSlot_/columns_->value());
    int target = -1, nextSlot = rows_->value()*columns_->value();
    for (int channel : visibleChannels()) {
        const auto cells = gridLayout_.value(channel);
        if (cells.contains(cell)) { target = channel; break; }
        const int slot = cells.y()*columns_->value()+cells.x();
        if (slot >= dropSlot_ && slot < nextSlot) { nextSlot = slot; target = channel; }
    }
    if (target < 0 && !visibleChannels().isEmpty()) target = visibleChannels().back();
    if (target >= 0) result.move(result.indexOf(pressedChannel_), result.indexOf(target));
    return result;
}
QHash<int, QRectF> ScopeWindow::panelPositions() const {
    QHash<int, QRectF> positions;
    for (int channel : displayedChannels()) positions.insert(channel, panelRect(channel));
    return positions;
}
void ScopeWindow::animateLayout(const QHash<int, QRectF> &from) {
    motion_->stop(); animationFrom_ = from; animationTo_.clear();
    for (int channel : displayedChannels()) animationTo_.insert(channel, cellsRect(displayLayout().value(channel)));
    motionClock_.start(); motion_->start();
}
void ScopeWindow::cancelDrag() {
    motion_->stop(); animationFrom_.clear(); animationTo_.clear();
    pressedChannel_ = -1; dragging_ = false; dropSlot_ = -1; unsetCursor();
    resizeChannel_ = -1; resizeEdges_ = 0; previewValid_ = false; previewLayout_.clear();
    pressedColor_ = hoveredColor_ = -1; setToolTip({});
    updateList();
}
void ScopeWindow::previewGesture(const QPointF &point) {
    const auto from = panelPositions();
    if (resizeChannel_ >= 0) {
        const auto first = cardRect(0);
        const qreal pitchX = columns_->value() > 1 ? cardRect(1).left()-first.left() : first.width()+10;
        const qreal pitchY = rows_->value() > 1 ? cardRect(columns_->value()).top()-first.top() : first.height()+10;
        const auto delta = point-dragStart_;
        const QSize span(std::clamp(originalSpan_.width()+(resizeEdges_&1 ? qRound(delta.x()/pitchX) : 0), 1, columns_->value()),
                         std::clamp(originalSpan_.height()+(resizeEdges_&2 ? qRound(delta.y()/pitchY) : 0), 1, rows_->value()));
        if (span == resizeSpan_) return;
        resizeSpan_ = span;
        auto spans = spans_; spans[resizeChannel_] = span;
        previewLayout_ = packScopes(rows_->value(), columns_->value(), order_, hidden_, spans);
        previewValid_ = previewLayout_.contains(resizeChannel_);
    } else {
        const int slot = slotAt(point);
        const bool moved = !dragging_ || dropSlot_ != slot;
        dragging_ = true; dragPosition_ = point; dropSlot_ = slot;
        if (!moved) { update(); return; }
        previewLayout_ = packScopes(rows_->value(), columns_->value(), dropOrder(), hidden_, spans_);
        previewValid_ = slot >= 0 && previewLayout_.contains(pressedChannel_);
    }
    animateLayout(from); updateList(); update();
}
QRectF ScopeWindow::gestureTarget() const {
    const int channel = resizeChannel_ >= 0 ? resizeChannel_ : pressedChannel_;
    if (previewValid_) return cellsRect(previewLayout_.value(channel));
    if (resizeChannel_ >= 0) {
        auto cells = gridLayout_.value(channel); cells.setSize(resizeSpan_);
        return cellsRect(cells.intersected(QRect(0, 0, columns_->value(), rows_->value())));
    }
    return dropSlot_ >= 0 ? cardRect(dropSlot_) : QRectF();
}
void ScopeWindow::mousePressEvent(QMouseEvent *event) {
    if (edit_->isChecked() && event->button() == Qt::LeftButton) {
        const int channel = panelAt(event->position());
        if (channel >= 0) {
            const auto card = panelRect(channel);
            if (colorButtonRect(card).contains(event->position())) {
                cancelDrag(); pressedColor_ = hoveredColor_ = channel; update(); event->accept(); return;
            }
            const int edges = resizeEdgesAt(card, event->position());
            if (edges) {
                cancelDrag(); setFocus(); resizeChannel_ = channel; resizeEdges_ = edges;
                setCursor(edges == 3 ? Qt::SizeFDiagCursor : edges == 1 ? Qt::SizeHorCursor : Qt::SizeVerCursor);
                originalSpan_ = resizeSpan_ = spans_[channel]; dragStart_ = event->position();
                previewLayout_ = gridLayout_; previewValid_ = true;
                channelList_->setCurrentRow(order_.indexOf(channel)); update(); event->accept(); return;
            }
            if (event->position().y() < card.top()+28) {
                if (event->position().x() > card.right()-28) { hidden_[channel] = true; layoutChanged(); }
                else {
                    cancelDrag(); setFocus(); pressedChannel_ = channel; dragStart_ = dragPosition_ = event->position(); dragCard_ = card;
                    channelList_->setCurrentRow(order_.indexOf(channel));
                }
                event->accept(); return;
            }
        }
    }
    FrameWindow::mousePressEvent(event);
}
void ScopeWindow::mouseMoveEvent(QMouseEvent *event) {
    if (pressedColor_ >= 0) { event->accept(); return; }
    if (resizeChannel_ >= 0 && (event->buttons() & Qt::LeftButton)) {
        previewGesture(event->position());
        setCursor(resizeEdges_ == 3 ? Qt::SizeFDiagCursor : resizeEdges_ == 1 ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        event->accept(); return;
    }
    if (pressedChannel_ >= 0 && (event->buttons() & Qt::LeftButton)) {
        if (dragging_ || (event->position()-dragStart_).manhattanLength() >= QApplication::startDragDistance()) {
            previewGesture(event->position()); setCursor(Qt::ClosedHandCursor);
        }
        event->accept(); return;
    }
    int hovered = -1, edges = 0;
    const int channel = edit_->isChecked() ? panelAt(event->position()) : -1;
    if (channel >= 0) {
        const auto card = panelRect(channel);
        if (colorButtonRect(card).contains(event->position())) hovered = channel;
        else edges = resizeEdgesAt(card, event->position());
    }
    if (hoveredColor_ != hovered) { hoveredColor_ = hovered; update(); }
    if (hovered >= 0) {
        setToolTip("Edit colors for "+channelName(hovered)); setCursor(Qt::PointingHandCursor); return;
    }
    if (edges) {
        setCursor(edges == 3 ? Qt::SizeFDiagCursor : edges == 1 ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        setToolTip("Drag to resize in grid cells. Escape cancels."); return;
    }
    // FrameWindow's event filter already restores the ordinary cursor or its
    // outer window resize grip. Do not overwrite that edge cursor here.
    setToolTip({}); FrameWindow::mouseMoveEvent(event);
}
void ScopeWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && resizeChannel_ >= 0) {
        previewGesture(event->position());
        const auto from = panelPositions();
        if (previewValid_ && cellsRect(QRect(0, 0, columns_->value(), rows_->value())).contains(event->position()))
            spans_[resizeChannel_] = resizeSpan_;
        layoutChanged(); animateLayout(from); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && pressedColor_ >= 0) {
        const int channel = pressedColor_;
        const bool activate = edit_->isChecked() && visibleChannels().contains(channel) && colorButtonRect(panelRect(channel)).contains(event->position());
        cancelDrag(); update(); event->accept();
        if (activate) { channelList_->setCurrentRow(order_.indexOf(channel)); editColors(channel); }
        return;
    }
    if (event->button() == Qt::LeftButton && pressedChannel_ >= 0) {
        if (dragging_) previewGesture(event->position());
        const bool animate = dragging_; const auto from = panelPositions();
        if (dragging_ && previewValid_) { order_ = dropOrder(); rebuildList(); }
        layoutChanged(); if (animate) animateLayout(from);
        event->accept(); return;
    }
    FrameWindow::mouseReleaseEvent(event);
}
void ScopeWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && (resizeChannel_ >= 0 || pressedChannel_ >= 0 || pressedColor_ >= 0)) {
        const auto from = panelPositions(); cancelDrag(); animateLayout(from); update(); event->accept(); return;
    }
    FrameWindow::keyPressEvent(event);
}
void ScopeWindow::leaveEvent(QEvent *event) {
    hoveredColor_ = -1; setToolTip({}); update(); FrameWindow::leaveEvent(event);
}
