#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
void pump(QApplication &app, int ms = 30) {
    QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
void until(QApplication &app, const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 8000) pump(app);
    require(ready(), "Shortcut transport did not settle.");
}
void key(QWidget &target, int code, Qt::KeyboardModifiers modifiers = {}, bool repeat = false) {
    const QString text = code >= Qt::Key_0 && code <= Qt::Key_9 ? QString(QChar(code)) : QString();
    QKeyEvent override(QEvent::ShortcutOverride, code, modifiers, text, repeat);
    QApplication::sendEvent(&target, &override);
    QKeyEvent press(QEvent::KeyPress, code, modifiers, text, repeat);
    QApplication::sendEvent(&target, &press);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers, text);
    QApplication::sendEvent(&target, &release);
}
void focus(QApplication &app, QWidget &widget) {
    widget.window()->activateWindow(); widget.setFocus(); pump(app);
}
}

void checkShortcuts(QApplication &app, const QString &directory, QTextStream &log) {
    PlayerWindow window; auto &player = window.player(); auto &scope = window.scopeWindow();
    player.setVolume(0); window.show();
    auto help = window.findChild<QPushButton *>("keyboardShortcuts");
    require(help && !scope.findChild<QPushButton *>("keyboardShortcuts"), "Help button must exist only in the main player.");
    key(window, Qt::Key_F5); key(window, Qt::Key_9, Qt::ControlModifier);
    require(!player.state().valid && !player.state().busy, "Empty-player shortcuts started transport.");
    window.loadFile(QDir(directory).filePath("demo.nsf"), 1);
    until(app, [&] { return !player.state().busy && player.state().positionMs > 200; }); window.refresh();
    scope.findChild<QPushButton *>("editLayout")->click();
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    auto view = scope.findChild<QComboBox *>("cardView");
    auto item = [&](int id) -> QListWidgetItem * {
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toInt() == id) return list->item(i);
        throw std::runtime_error("Missing shortcut test card.");
    };
    auto kind = [&](int id) { list->setCurrentItem(item(id)); return view->currentIndex(); };
    focus(app, *list);
    key(*list, Qt::Key_1); require(kind(0) == 1 && kind(1) == 0, "Digit changed the wrong card.");
    key(scope, Qt::Key_1, Qt::KeypadModifier); require(kind(0) == 2, "Numpad did not cycle to keyboard.");
    key(window, Qt::Key_1); require(kind(0) == 0, "View cycle did not return to waveform.");
    const auto generation = player.state().generation;
    key(scope, Qt::Key_2, Qt::ControlModifier | Qt::KeypadModifier);
    require(player.state().muteMask == 2 && item(1)->checkState() == Qt::Checked, "Mute changed visibility or wrong voice.");
    key(scope, Qt::Key_2, Qt::ControlModifier, true);
    require(player.state().muteMask == 2, "Auto-repeat toggled mute again.");
    key(scope, Qt::Key_2, Qt::AltModifier);
    require(item(1)->checkState() == Qt::Unchecked && player.state().muteMask == 2, "Hide changed sound.");
    key(window, Qt::Key_2, Qt::AltModifier | Qt::KeypadModifier);
    require(item(1)->checkState() == Qt::Checked, "Hidden channel did not return.");
    key(window, Qt::Key_9, Qt::ControlModifier);
    require(player.state().muteMask == 2 && player.state().generation == generation, "Absent channel or visual edit changed transport.");

    // Reorder through the same item-model signal used by drag/drop.
    require(list->model()->moveRow({}, 0, {}, 3), "Cannot reorder shortcut test cards.");
    key(scope, Qt::Key_1); require(kind(0) == 1 && kind(1) == 0, "Numbers followed grid slots instead of source channels.");
    QTimer::singleShot(0, &scope, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto sources = dialog->findChild<QComboBox *>("newCardSource");
        sources->setCurrentIndex(sources->findData(0));
        dialog->findChild<QComboBox *>("newCardView")->setCurrentIndex(0); dialog->accept();
    });
    scope.findChild<QPushButton *>("addScopeCard")->click();
    const int duplicate = list->currentItem()->data(Qt::UserRole).toInt();
    focus(app, *list); key(scope, Qt::Key_1);
    require(kind(0) == 2 && kind(duplicate) == 2, "Duplicate views did not cycle together.");
    key(window, Qt::Key_1, Qt::AltModifier);
    require(item(0)->checkState() == Qt::Unchecked && item(duplicate)->checkState() == Qt::Unchecked, "Duplicate visibility was not grouped.");
    for (int mode = 0; mode < 3; ++mode) {
        key(scope, Qt::Key_F2+mode);
        for (int i = 0; i < list->count(); ++i)
            require(kind(list->item(i)->data(Qt::UserRole).toInt()) == mode, "All-card shortcut missed hidden/mix/duplicate card.");
    }
    require(item(0)->checkState() == Qt::Unchecked, "All-card shortcut changed visibility.");
    scope.hide(); key(window, Qt::Key_1, Qt::AltModifier);
    require(!scope.isVisible() && item(0)->checkState() == Qt::Checked, "Hidden window shortcut opened the window or failed.");
    scope.show(); focus(app, *list);

    auto rows = scope.findChild<QSpinBox *>("gridRows");
    auto input = rows->findChild<QLineEdit *>(); focus(app, *input); input->selectAll();
    key(*input, Qt::Key_1);
    require(input->text() == "1" && kind(0) == 2, "Typing a grid size triggered a view shortcut.");
    key(*input, Qt::Key_F8); require(!player.state().busy && player.state().playing, "Function key escaped the focused field.");
    focus(app, *view); key(*view, Qt::Key_F2);
    require(kind(0) == 2, "Dropdown intercepted by app shortcut.");
    view->showPopup();
    until(app, [&] { return QApplication::activePopupWidget(); });
    key(window, Qt::Key_2, Qt::ControlModifier);
    require(player.state().muteMask == 2, "Shortcut escaped a dropdown popup."); view->hidePopup();
    QDialog other(&window); other.setModal(true); other.show(); pump(app);
    key(window, Qt::Key_2, Qt::ControlModifier);
    require(player.state().muteMask == 2, "Shortcut escaped a dialog."); other.close();
    focus(app, *list);
    log << "Shortcuts: both windows, numpad, stable channel numbers, duplicate/hidden cards, mute/visibility independence, fields/dialog guards PASS\n"; log.flush();

    key(scope, Qt::Key_F8); until(app, [&] { return !player.state().busy && !player.state().playing; });
    auto paused = player.state(); key(window, Qt::Key_F8);
    require(player.state().generation == paused.generation, "F8 reissued pause while already paused.");
    key(window, Qt::Key_F7); until(app, [&] { return !player.state().busy && player.state().playing; });
    const auto resumed = player.state(); key(scope, Qt::Key_F7);
    require(player.state().generation == resumed.generation, "F7 reissued resume while already playing.");
    player.seek(3000, false); until(app, [&] { return !player.state().busy; });
    player.setOutputMask(1); key(scope, Qt::Key_F5);
    until(app, [&] { return !player.state().busy && player.state().playing; });
    require(player.state().positionMs < 1000 && player.state().info.song == 1 && player.state().muteMask == 2 && player.state().outputMask == 1,
        "F5 did not restart current track with existing mute/output settings.");
    const auto size = scope.size(); const auto theme = scope.findChild<QComboBox *>("scopeTheme")->currentText();
    scope.findChild<QCheckBox *>("channelNumbers")->setChecked(false);
    key(window, Qt::Key_R, Qt::ControlModifier | Qt::ShiftModifier);
    require(player.state().muteMask == 0 && player.state().outputMask == 3 && list->count() == 6 &&
        scope.visibleChannels() == QVector<int>({0,1,2,3,4}) && rows->value() == 5 &&
        scope.findChild<QCheckBox *>("channelNumbers")->isChecked() && item(ScopeTheme::FullMix)->checkState() == Qt::Unchecked,
        "Reset did not restore channel sound/default grid/cards/labels.");
    for (int i = 0; i < list->count(); ++i) require(kind(list->item(i)->data(Qt::UserRole).toInt()) == 0, "Reset retained a non-waveform view.");
    require(scope.size() == size && scope.findChild<QComboBox *>("scopeTheme")->currentText() == theme, "Reset changed size or theme.");
    window.grab().save(QDir(directory).filePath("native-shortcuts-player.png"));

    int dialogs = 0; bool guideValid = true;
    auto closeGuide = [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) { guideValid = false; return; }
        auto guide = dialog->findChild<QTextBrowser *>("shortcutsGuide");
        guideValid &= guide && guide->toPlainText().contains("F8") && guide->toPlainText().contains("Reset channels and layout");
        ++dialogs; dialog->grab().save(QDir(directory).filePath("native-shortcuts-help.png")); dialog->accept();
    };
    for (QWidget *target : {static_cast<QWidget *>(&window), static_cast<QWidget *>(&scope)}) {
        QTimer::singleShot(100, &window, closeGuide); key(*target, Qt::Key_F1);
    }
    QTimer::singleShot(100, &window, closeGuide); help->click();
    require(guideValid && dialogs == 3, "Help button/F1 guide did not open.");
    require(player.state().outputErrors == 0 && player.state().starvations == 0, "Shortcut check interrupted audio output.");
    window.close();
    log << "Shortcuts: restart/resume/pause, reset, help button/F1 and silent audio continuity PASS\n"; log.flush();
}
