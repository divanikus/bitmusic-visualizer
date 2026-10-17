#include "scopewindow.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <algorithm>
#include <limits>
#include <tuple>

void ScopeWindow::selectCard(int id) {
    const int row = cards_.order().indexOf(id);
    if (row < 0) return;
    cardSelectionActive_ = true;
    channelList_->setCurrentRow(row);
    channelList_->scrollToItem(channelList_->currentItem());
    updateCardSelection();
}
void ScopeWindow::clearCardSelection() {
    cardSelectionActive_ = false;
    updateCardSelection();
}
void ScopeWindow::updateCardSelection() {
    const int id = selectedCardId();
    if (!cardSelectionHint_) {
        cardSelectionHint_ = new QLabel(this);
        cardSelectionHint_->setObjectName("cardSelectionHint");
        cardSelectionHint_->setAttribute(Qt::WA_TransparentForMouseEvents);
        cardSelectionHint_->setStyleSheet("QLabel { color: #dce5e8; background: #202e35; border: 1px solid #a2e5d5; border-radius: 5px; padding: 6px; }");
    }
    const bool hidden = cardSelectionActive_ && id >= 0 && cards_.order().contains(id) &&
        state_.valid && !newFile_ && !gridLayout_.contains(id);
    cardSelectionHint_->setVisible(hidden);
    if (hidden) {
        const auto area = cellsRect(QRect(0, 0, columns_->value(), rows_->value()));
        const QString suffix = cards_[id].hidden ? " · Hidden · H to show" : " · Outside grid · Increase grid size";
        const int width = std::max(1, int(area.width())-24);
        cardSelectionHint_->setText(cardSelectionHint_->fontMetrics().elidedText(channelName(id)+suffix, Qt::ElideRight, width-16));
        cardSelectionHint_->adjustSize();
        cardSelectionHint_->move(qRound(area.left()+8), qRound(area.bottom()-cardSelectionHint_->height()-8));
        cardSelectionHint_->raise();
    }
    update();
}
void ScopeWindow::moveCardSelection(int key) {
    auto ids = visibleChannels();
    if (ids.isEmpty()) return;
    // Grid position, not source number or list index; large cards can leave holes.
    std::sort(ids.begin(), ids.end(), [this](int a, int b) {
        const auto ar = gridLayout_.value(a), br = gridLayout_.value(b);
        return std::make_pair(ar.top(), ar.left()) < std::make_pair(br.top(), br.left());
    });
    const int current = selectedCardId();
    if (!cardSelectionActive_ || !gridLayout_.contains(current)) {
        selectCard(key == Qt::Key_Up || key == Qt::Key_Left ? ids.back() : ids.front());
        return;
    }
    const auto from = gridLayout_.value(current);
    const bool horizontal = key == Qt::Key_Left || key == Qt::Key_Right;
    int best = -1;
    auto score = std::make_tuple(2, std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    for (int id : ids) {
        if (id == current) continue;
        const auto to = gridLayout_.value(id);
        const int forward = key == Qt::Key_Right ? to.left()-from.right() : key == Qt::Key_Left ? from.left()-to.right() :
            key == Qt::Key_Down ? to.top()-from.bottom() : from.top()-to.bottom();
        if (forward <= 0) continue;
        const int cross = horizontal ? std::max(to.top()-from.bottom(), from.top()-to.bottom()) :
            std::max(to.left()-from.right(), from.left()-to.right());
        const auto candidate = std::make_tuple(cross > 0 ? 1 : 0, forward, std::max(0, cross));
        if (candidate < score) { best = id; score = candidate; }
    }
    if (best >= 0) selectCard(best);
}
bool ScopeWindow::handleCardShortcut(QKeyEvent *event, QWidget *target) {
    const auto focus = QApplication::focusWidget();
    const auto inList = [this](QWidget *w) { return w && (w == channelList_ || channelList_->isAncestorOf(w)); };
    if ((target != this && !inList(target)) || (focus && focus != this && !inList(focus))) return false;
    const int key = event->key();
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const bool resize = arrow && modifiers == Qt::ShiftModifier;
    if (modifiers != Qt::NoModifier && !resize) return false;
    if (resize && !cardSelectionActive_) return false;
    if (arrow && !resize && inList(focus)) {
        // Native list navigation/scrolling, even at an edge where the row stays the same.
        if (event->type() == QEvent::KeyPress && state_.valid && !state_.busy && !newFile_) selectCard(selectedCardId());
        return false;
    }
    const bool escape = key == Qt::Key_Escape && cardSelectionActive_ && !presentationFullscreen();
    if (!arrow && !escape && key != Qt::Key_V && key != Qt::Key_M && key != Qt::Key_S &&
        key != Qt::Key_H && key != Qt::Key_L && key != Qt::Key_Return && key != Qt::Key_Enter) return false;
    if (pressedChannel_ >= 0 || resizeChannel_ >= 0 || pressedColor_ >= 0 || pressedSolo_ >= 0) return false;
    if (!arrow && !cardSelectionActive_) return false;
    event->accept();
    if (event->type() == QEvent::ShortcutOverride || (event->isAutoRepeat() && !arrow)) return true;
    if (escape) { clearCardSelection(); return true; }
    if (!state_.valid || state_.busy || newFile_) return true;
    if (arrow && !resize) { moveCardSelection(key); return true; }
    const int id = selectedCardId();
    if (id < 0 || !cards_.order().contains(id)) return true;
    if (resize) {
        auto span = cards_[id].span;
        if (key == Qt::Key_Left || key == Qt::Key_Right)
            span.rwidth() += key == Qt::Key_Right ? 1 : -1;
        else span.rheight() += key == Qt::Key_Down ? 1 : -1;
        span = clampScopeSpan(span, rows_->value(), columns_->value());
        if (span == cards_[id].span) return true;
        // Like mouse resizing, do not push the selected visible card out of the grid.
        // Hidden/overflow cards can still be resized from the list, including recovery.
        if (gridLayout_.contains(id) && !cards_.pack(rows_->value(), columns_->value(), cards_.order(), id, span).contains(id))
            return true;
        applyCardSize(id, span);
        return true;
    }
    const int source = sourceFor(id);
    if (key == Qt::Key_M || key == Qt::Key_S) {
        if (source >= 0 && source < state_.info.voices.size()) {
            if (key == Qt::Key_M && onMuteRequested) onMuteRequested(source);
            if (key == Qt::Key_S && onSoloRequested) onSoloRequested(source);
        }
    } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        editColors(id);
    } else {
        auto &card = cards_[id];
        if (key == Qt::Key_V) {
            card.kind = ScopeViewSupport{bool(state_.info.noteRoll), bool(state_.info.patterns)}.nextView(card.kind);
        } else if (key == Qt::Key_L) card.levels = !card.levels;
        else if (key == Qt::Key_H) cards_.setHidden(id, !card.hidden);
        layoutChanged(); updateViewEditor();
    }
    return true;
}
