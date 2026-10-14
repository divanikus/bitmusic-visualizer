#include "window.h"
#include "shortcuts.h"
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
class ShortcutsButton : public QPushButton {
public:
    ShortcutsButton() {
        setObjectName("keyboardShortcuts"); setFixedSize(30, 26);
        setToolTip("Keyboard shortcuts (F1)"); setAccessibleName(toolTip()); setCursor(Qt::PointingHandCursor);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen); p.setBrush(QColor(underMouse() || hasFocus() ? "#30434a" : "#202a30"));
        p.drawRoundedRect(rect(), 6, 6);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(QColor("#b8cccf"), 1.3));
        p.drawRoundedRect(QRectF(4, 6, 22, 14), 2, 2);
        for (int y : {10, 13}) for (int x : {8, 12, 16, 20}) p.drawLine(x, y, x+1, y);
        p.drawLine(10, 17, 20, 17);
    }
};
bool editing(QWidget *widget) {
    for (auto current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<QLineEdit *>(current) || qobject_cast<QAbstractSpinBox *>(current) ||
            qobject_cast<QComboBox *>(current) || qobject_cast<QTextEdit *>(current) || qobject_cast<QPlainTextEdit *>(current)) return true;
    }
    return false;
}
}

void PlayerWindow::setupShortcutsHelp(TitleBar *bar) {
    auto button = new ShortcutsButton; bar->addAction(button);
    connect(button, &QPushButton::clicked, this, [this] { showShortcutsHelp(); });
}
void PlayerWindow::showShortcutsHelp() {
    QDialog dialog(this); dialog.setWindowTitle("Keyboard shortcuts"); dialog.setObjectName("shortcutsDialog");
    auto layout = new QVBoxLayout(&dialog);
    auto text = new QTextBrowser; text->setObjectName("shortcutsGuide");
    text->setStyleSheet("QTextBrowser { background: #1b252b; color: #dce5e8; border: none; padding: 8px; }");
    text->setHtml(QString(R"(
      <h3>Channels</h3>
      <table cellspacing="8">
      <tr><td><b>1–9</b></td><td>Cycle Waveform → Spectrum → Keyboard</td></tr>
      <tr><td><b>%1–9</b></td><td>Mute / unmute channel</td></tr>
      <tr><td><b>%2–9</b></td><td>Hide / show channel cards</td></tr>
      <tr><td><b>F2 / F3 / F4</b></td><td>All cards: Waveform / Spectrum / Keyboard</td></tr>
      <tr><td><b>%3</b></td><td>Reset channels and layout</td></tr>
      </table>
      <h3>Playback</h3>
      <table cellspacing="8">
      <tr><td><b>F5</b></td><td>Play the current track from the beginning</td></tr>
      <tr><td><b>F7</b></td><td>Resume playback</td></tr>
      <tr><td><b>F8</b></td><td>Pause playback</td></tr>
      <tr><td><b>F1</b></td><td>This guide</td></tr>
      </table>
      <p>Works in either player window. Use the number row or the numpad with
      Num Lock on. Numbers refer to the original channel numbers, even after
      reordering. Multiple cards of one channel change together.</p>
      <p>Reset restores waveform cards, the default grid, labels and visibility,
      and enables all voices and both audio outputs. Full mix starts hidden.
      Colors, effects, volume and window size are kept.</p>
      <p>Shortcuts are inactive while editing fields, using dropdowns or dialogs.
      F1 also works from fields. No global system shortcuts are registered.</p>
    )").arg(shortcutLabel(Qt::ControlModifier, Qt::Key_1).toHtmlEscaped(),
        shortcutLabel(Qt::AltModifier, Qt::Key_1).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_R).toHtmlEscaped()));
    layout->addWidget(text);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.resize(520, 500); dialog.exec();
}

bool PlayerWindow::eventFilter(QObject *object, QEvent *event) {
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return FrameWindow::eventFilter(object, event);
    auto widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget->window() != this && widget->window() != &scopes_) ||
        QApplication::activeModalWidget() || QApplication::activePopupWidget()) return FrameWindow::eventFilter(object, event);
    auto keyEvent = static_cast<QKeyEvent *>(event);
    const auto modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
    const int key = keyEvent->key();
    const bool help = key == Qt::Key_F1 && modifiers == Qt::NoModifier;
    // Unhandled keys bubble to parents. Keep protecting the focused editor there too.
    const auto focus = QApplication::focusWidget();
    if (!help && (editing(widget) || (focus && focus->window() == widget->window() && editing(focus))))
        return FrameWindow::eventFilter(object, event);
    const bool digit = key >= Qt::Key_1 && key <= Qt::Key_9 &&
        (modifiers == Qt::NoModifier || modifiers == Qt::ControlModifier || modifiers == Qt::AltModifier);
    const bool reset = key == Qt::Key_R && modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
    const bool function = modifiers == Qt::NoModifier &&
        ((key >= Qt::Key_F2 && key <= Qt::Key_F5) || key == Qt::Key_F7 || key == Qt::Key_F8);
    if (!help && !digit && !reset && !function) return FrameWindow::eventFilter(object, event);
    event->accept();
    if (event->type() == QEvent::ShortcutOverride || keyEvent->isAutoRepeat()) return true;
    if (help) { showShortcutsHelp(); return true; }
    const auto state = player_.state();
    if (!state.valid || state.busy) return true;
    refreshScopes(); // Keep source indices current even when scopes are hidden.
    if (digit) {
        const int source = key - Qt::Key_1;
        if (source >= state.info.voices.size()) return true;
        if (modifiers == Qt::ControlModifier) player_.setMuteMask(state.muteMask ^ (uint32_t(1) << source));
        else if (modifiers == Qt::AltModifier) scopes_.toggleSourceVisibility(source);
        else scopes_.cycleSourceView(source);
    } else if (reset) {
        player_.setMuteMask(0); player_.setOutputMask(3); scopes_.resetLayout();
    } else if (key >= Qt::Key_F2 && key <= Qt::Key_F4) {
        scopes_.setAllViews(ScopeWindow::ViewKind(key-Qt::Key_F2));
    } else if (key == Qt::Key_F5) player_.seek(0, true);
    else if (key == Qt::Key_F7 && !state.playing) player_.seek(state.positionMs, true);
    else if (key == Qt::Key_F8 && state.playing) player_.seek(state.positionMs, false);
    refresh();
    return true;
}
