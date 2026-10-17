#include "playerwindow.h"
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
#include <QShortcut>
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

QPushButton *createShortcutsHelpButton(QWidget *owner) {
    auto button = new ShortcutsButton;
    QObject::connect(button, &QPushButton::clicked, owner, [owner] { showShortcutsHelp(owner); });
    return button;
}

void PlayerWindow::setupShortcuts() {
    // Qt resolves shifted number-row keys through the current keyboard layout.
    // Raw key() can instead contain '!' / '@' / other national-layout symbols.
    for (QWidget *owner : {static_cast<QWidget *>(this), static_cast<QWidget *>(&scopes_)}) {
        for (int source = 0; source < 9; ++source) {
            auto shortcut = new QShortcut(QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier,
                Qt::Key(Qt::Key_1 + source))), owner);
            shortcut->setObjectName(QString("soloShortcut%1").arg(source)); shortcut->setAutoRepeat(false);
            connect(shortcut, &QShortcut::activated, this, [this, source] {
                if (QApplication::activeModalWidget() || QApplication::activePopupWidget() ||
                    editing(QApplication::focusWidget())) return;
                player_.toggleSolo(source); refresh();
            });
        }
    }
}
void showShortcutsHelp(QWidget *owner) {
    QDialog dialog(owner); dialog.setWindowTitle("Keyboard shortcuts"); dialog.setObjectName("shortcutsDialog");
    auto layout = new QVBoxLayout(&dialog);
    auto text = new QTextBrowser; text->setObjectName("shortcutsGuide");
    text->setStyleSheet("QTextBrowser { background: #1b252b; color: #dce5e8; border: none; padding: 8px; }");
    text->setHtml(QString(R"(
      <h3>Channels</h3>
      <table cellspacing="8">
      <tr><td><b>1–9</b></td><td>Cycle available views: Waveform, Spectrum, Keyboard, Piano roll or Pattern</td></tr>
      <tr><td><b>%1–9</b></td><td>Mute / unmute channel</td></tr>
      <tr><td><b>%2–9</b></td><td>Hide / show channel cards</td></tr>
      <tr><td><b>%7–9</b></td><td>Solo channel / restore previous channels</td></tr>
      <tr><td><b>Shift+click</b></td><td>Solo a visualization card or a button in Channels; repeat to restore</td></tr>
      <tr><td><b>0 / %4</b></td><td>Full mix: cycle view / hide or show</td></tr>
      <tr><td><b>F2 / F3 / F4</b></td><td>All cards: Waveform / Spectrum / Keyboard</td></tr>
      <tr><td><b>%3</b></td><td>Reset channels and layout</td></tr>
      </table>
      <h3>Playback</h3>
      <table cellspacing="8">
      <tr><td><b>%5 / %6</b></td><td>Toggle left / right audio output</td></tr>
      <tr><td><b>F5</b></td><td>Play the current track from the beginning</td></tr>
      <tr><td><b>F7</b></td><td>Resume playback</td></tr>
      <tr><td><b>F8</b></td><td>Pause playback</td></tr>
      <tr><td><b>F1</b></td><td>This guide</td></tr>
      <tr><td><b>F11 / Esc</b></td><td>Toggle full-screen visualizer / leave full screen</td></tr>
      </table>
      <p>Works in either player window. Use the number row or the numpad with
      Num Lock on. Numbers refer to the original channel numbers, even after
      reordering. Multiple cards of one channel change together.</p>
      <p>Solo temporarily plays only one voice. Shift-click another channel to
      switch Solo, or the same one to restore the previous mutes. Ordinary mute
      controls exit Solo and edit the currently audible set.</p>
      <p>Reset restores waveform cards, the default grid, labels and visibility,
      and enables all voices and both audio outputs. Full mix starts hidden.
      Colors, effects, volume and window size are kept.</p>
      <p>Shortcuts are inactive while editing fields, using dropdowns or dialogs.
      F1 also works from fields. No global system shortcuts are registered.</p>
      <h3>Playlist</h3>
      <p><b>%8</b>: Open · <b>%9</b>: Add · <b>Delete</b>: Remove.<br>
      Drag to reorder; Ctrl/Shift-click to select multiple tracks.</p>
    )").arg(shortcutLabel(Qt::ControlModifier, Qt::Key_1).toHtmlEscaped(),
        shortcutLabel(Qt::AltModifier, Qt::Key_1).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_R).toHtmlEscaped(),
        shortcutLabel(Qt::AltModifier, Qt::Key_0).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier, Qt::Key_Left).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier, Qt::Key_Right).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_1).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier, Qt::Key_O).toHtmlEscaped(),
        shortcutLabel(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_O).toHtmlEscaped()));
    layout->addWidget(text);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
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
    const bool fullscreen = modifiers == Qt::NoModifier &&
        (key == Qt::Key_F11 || (key == Qt::Key_Escape && scopes_.presentationFullscreen()));
    // Unhandled keys bubble to parents. Keep protecting the focused editor there too.
    const auto focus = QApplication::focusWidget();
    if (!help && (editing(widget) || (focus && focus->window() == widget->window() && editing(focus))))
        return FrameWindow::eventFilter(object, event);
    const bool digit = key >= Qt::Key_1 && key <= Qt::Key_9 &&
        (modifiers == Qt::NoModifier || modifiers == Qt::ControlModifier || modifiers == Qt::AltModifier);
    const bool mix = key == Qt::Key_0 && (modifiers == Qt::NoModifier || modifiers == Qt::AltModifier);
    const bool output = modifiers == Qt::ControlModifier && (key == Qt::Key_Left || key == Qt::Key_Right);
    const bool reset = key == Qt::Key_R && modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
    const bool function = modifiers == Qt::NoModifier &&
        ((key >= Qt::Key_F2 && key <= Qt::Key_F5) || key == Qt::Key_F7 || key == Qt::Key_F8);
    if (!help && !fullscreen && !digit && !mix && !output && !reset && !function) return FrameWindow::eventFilter(object, event);
    event->accept();
    if (event->type() == QEvent::ShortcutOverride || keyEvent->isAutoRepeat()) return true;
    if (help) { showShortcutsHelp(widget->window()); return true; }
    if (fullscreen) { scopes_.toggleFullscreen(); return true; }
    const auto state = player_.state();
    if (!state.valid || state.busy) return true;
    refreshScopes(); // Keep source indices current even when scopes are hidden.
    if (digit || mix) {
        const int source = mix ? ScopeTheme::FullMix : key - Qt::Key_1;
        if (!mix && source >= state.info.voices.size()) return true;
        if (modifiers == Qt::ControlModifier) player_.setMuteMask(state.muteMask ^ (VoiceMask::bit(source)));
        else if (modifiers == Qt::AltModifier) scopes_.toggleSourceVisibility(source);
        else scopes_.cycleSourceView(source);
    } else if (output) {
        player_.setOutputMask(state.outputMask ^ (key == Qt::Key_Left ? 1u : 2u));
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
