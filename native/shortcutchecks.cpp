#include "playerwindow.h"
#include "playerchannels.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <stdexcept>

void checkSelectedCardShortcuts(QApplication &app, const QString &directory, QTextStream &log);

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
    widget.window()->activateWindow(); pump(app); widget.setFocus(); pump(app);
}
void click(QWidget &target, Qt::KeyboardModifiers modifiers = {}) {
    const QPointF point = target.rect().center(), global = target.mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(&target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, modifiers);
    QApplication::sendEvent(&target, &release);
}
void checkChannelPresentation(QTextStream &log) {
    PlayerChannels channels;
    int requests = 0, requestedSource = -1;
    bool requestedAudible = true;
    channels.onAudibleChanged = [&](int source, bool audible) { ++requests; requestedSource = source; requestedAudible = audible; };
    PlayerState state; state.valid = true;
    for (int i = 0; i < 70; ++i) { state.info.voices << QString("Source %1").arg(i); state.instruments << QString(); }
    state.muteMask = ~VoiceMask::bit(65); state.soloChannel = 65;
    channels.rebuild(state);
    auto high = channels.findChild<QToolButton *>("voice65");
    require(channels.count() == 70 && high && high->isChecked() && high->property("solo").toBool() && requests == 0,
        "Channel snapshot emitted a mute request or lost a high source index.");
    state.instruments[65] = "Lead"; state.muteMask = VoiceMask::bit(69); state.soloChannel = -1;
    channels.present(state);
    require(channels.findChild<QToolButton *>("voice65") == high && high->text() == "66  Source 65 · Lead" &&
        high->toolTip().startsWith("Source 65 / Lead\n") && high->accessibleName() == high->text() &&
        high->isChecked() && !high->property("solo").toBool() && requests == 0,
        "Instrument/mute refresh rebuilt channel buttons, emitted a request or left stale labels.");
    high->click();
    require(requests == 1 && requestedSource == 65 && !requestedAudible, "High channel request used another source or audible state.");
    state.busy = true; channels.present(state);
    require(!high->isEnabled() && requests == 1, "Busy snapshot enabled a channel or emitted a mute request.");
    state.info.voices.clear(); channels.rebuild(state);
    require(channels.count() == 0 && !channels.findChild<QToolButton *>("voice65"), "Channel rebuild retained obsolete source buttons.");
    log << "Channel controls: high source IDs, snapshot signal suppression, stable instrument labels and busy/rebuild state PASS\n"; log.flush();
}
void checkCardSolo(QApplication &app, PlayerWindow &window, QTextStream &log) {
    auto &player = window.player(); auto &scope = window.scopeWindow();
    auto edit = scope.findChild<QPushButton *>("editLayout");
    const bool wasEditing = edit->isChecked();
    const auto windowSize = scope.size();
    if (wasEditing) edit->click();
    scope.resize(1050, 700); pump(app);
    const auto before = player.state(); const auto visibility = scope.visibleChannels();
    auto mouse = [&](QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier) {
        QMouseEvent event(type, point, scope.mapToGlobal(point.toPoint()),
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        QApplication::sendEvent(&scope, &event);
    };
    auto clickAt = [&](QPointF point, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier,
                       QEvent::Type type = QEvent::MouseButtonPress) {
        mouse(type, point, modifiers); mouse(QEvent::MouseButtonRelease, point, modifiers);
    };
    for (auto kind : {ScopeWindow::ViewKind::Waveform, ScopeWindow::ViewKind::Spectrum, ScopeWindow::ViewKind::Keyboard}) {
        scope.setAllViews(kind);
        clickAt(scope.panelRect(1).center());
        require(player.state().soloChannel == 1 && player.state().muteMask == ~VoiceMask(2), "Card body did not solo its voice.");
        clickAt(scope.panelRect(0).center());
        require(player.state().soloChannel == 0, "Another card did not switch Solo.");
        clickAt(scope.panelRect(0).center(), Qt::ShiftModifier, QEvent::MouseButtonDblClick);
        require(player.state().soloChannel == -1 && player.state().muteMask == before.muteMask && !scope.presentationFullscreen(),
            "Rapid repeated Shift-click did not restore mutes or toggled fullscreen.");
    }
    clickAt(scope.panelRect(1).center(), Qt::NoModifier);
    clickAt(scope.panelRect(1).center(), Qt::ShiftModifier | Qt::ControlModifier);
    const auto start = scope.panelRect(1).center();
    mouse(QEvent::MouseButtonPress, start); mouse(QEvent::MouseMove, start+QPointF(40, 0));
    mouse(QEvent::MouseMove, start); mouse(QEvent::MouseButtonRelease, start);
    mouse(QEvent::MouseButtonPress, start); mouse(QEvent::MouseButtonRelease, scope.panelRect(0).center());
    mouse(QEvent::MouseButtonPress, start); key(scope, Qt::Key_Escape); mouse(QEvent::MouseButtonRelease, start);
    mouse(QEvent::MouseButtonPress, start); mouse(QEvent::MouseButtonRelease, start, Qt::NoModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == before.muteMask,
        "Ordinary/modified click or cancelled card gesture changed Solo.");
    require(scope.visibleChannels() == visibility && player.state().generation == before.generation &&
        player.state().outputMask == before.outputMask, "Card Solo changed layout, transport or L/R outputs.");

    edit->click(); scope.findChild<QSpinBox *>("gridRows")->setValue(4);
    scope.findChild<QSpinBox *>("gridColumns")->setValue(2);
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    QTimer::singleShot(0, &scope, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto sources = dialog->findChild<QComboBox *>("newCardSource"); sources->setCurrentIndex(sources->findData(1));
        dialog->findChild<QComboBox *>("newCardView")->setCurrentIndex(2); dialog->accept();
    });
    scope.findChild<QPushButton *>("addScopeCard")->click();
    const int duplicate = list->currentItem()->data(Qt::UserRole).toInt();
    require(list->model()->moveRow({}, list->currentRow(), {}, 0), "Cannot reorder duplicate Solo card.");
    pump(app, 200);
    clickAt(scope.panelRect(duplicate).center());
    require(player.state().soloChannel == 1, "Moved duplicate card soloed its grid position instead of its source.");
    clickAt(scope.panelRect(1).center());
    require(player.state().soloChannel == -1 && player.state().muteMask == before.muteMask,
        "Original card did not restore Solo selected through a duplicate.");
    const auto card = scope.panelRect(duplicate); const auto shown = scope.visibleChannels();
    clickAt(QPointF(card.right()-15, card.top()+14));
    clickAt(card.bottomRight()-QPointF(3, 3));
    require(player.state().soloChannel == -1 && scope.panelRect(duplicate) == card && scope.visibleChannels() == shown,
        "Shift-click on an editor header/resize grip changed the layout.");
    scope.toggleSourceVisibility(ScopeTheme::FullMix); pump(app, 200);
    require(!scope.panelRect(ScopeTheme::FullMix).isEmpty(), "Full mix Solo check is not visible.");
    clickAt(scope.panelRect(ScopeTheme::FullMix).center());
    require(player.state().soloChannel == -1 && player.state().muteMask == before.muteMask, "Full mix changed Solo.");
    scope.toggleFullscreen(); pump(app);
    clickAt(scope.panelRect(duplicate).center());
    require(player.state().soloChannel == 1 && scope.presentationFullscreen(), "Fullscreen card Solo failed.");
    clickAt(scope.panelRect(1).center()); scope.toggleFullscreen(); pump(app);
    scope.resetLayout(); if (edit->isChecked() != wasEditing) edit->click();
    scope.resize(windowSize); window.refresh();
    require(player.state().soloChannel == -1 && player.state().muteMask == before.muteMask &&
        player.state().generation == before.generation, "Card Solo did not restore original audio state.");
    log << "Card Solo: all views, normal/edit/fullscreen, moved duplicates, switch/restore, Full mix and gesture cancellation PASS\n";
}
void checkStereoShortcut(QApplication &app, PlayerWindow &window, const QString &directory, QTextStream &log) {
    auto &player = window.player(); auto &scope = window.scopeWindow();
    auto stereo = scope.findChild<QCheckBox *>("scopeStereo");
    const bool emptyPreference = stereo->isChecked();
    key(window, Qt::Key_S, Qt::ShiftModifier);
    require(stereo->isChecked() == emptyPreference, "Stereo shortcut changed the empty player.");
    window.loadFile(QDir(directory).filePath("demo.spc"));
    until(app, [&] { return player.state().valid && !player.state().busy; }); window.refresh();
    require(stereo->isEnabled() && stereo->toolTip().contains("Stereo ("), "Stereo shortcut hint or capability missing.");
    stereo->setChecked(false); scope.hide(); focus(app, window);
    const auto initial = player.state();
    key(window, Qt::Key_S, Qt::ShiftModifier);
    require(stereo->isChecked() && !scope.isVisible(), "Stereo shortcut failed from the player or opened hidden scopes.");
    QKeyEvent claim(QEvent::ShortcutOverride, Qt::Key_S, Qt::ShiftModifier);
    claim.setAccepted(false); QApplication::sendEvent(&window, &claim);
    key(window, Qt::Key_S, Qt::ShiftModifier, true);
    key(window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    require(claim.isAccepted() && stereo->isChecked(), "Override, repeat or extra modifiers toggled Stereo.");
    scope.show(); focus(app, scope); key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked(), "Stereo shortcut failed on the canvas without a selection.");
    key(scope, Qt::Key_Right); key(scope, Qt::Key_S);
    const auto solo = player.state().soloChannel;
    require(solo >= 0 && !stereo->isChecked(), "Plain S did not retain selected-card Solo.");
    key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(stereo->isChecked() && player.state().soloChannel == solo, "Shift+S interfered with Solo.");
    key(scope, Qt::Key_S);
    scope.findChild<QPushButton *>("editLayout")->setChecked(true);
    auto list = scope.findChild<QListWidget *>("scopeChannelList"); focus(app, *list);
    key(*list, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked(), "Stereo shortcut failed in the Cards list.");
    auto input = scope.findChild<QSpinBox *>("gridRows")->findChild<QLineEdit *>();
    focus(app, *input); key(*input, Qt::Key_S, Qt::ShiftModifier); key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked(), "Stereo shortcut escaped a focused field.");
    auto combo = scope.findChild<QComboBox *>("cardView"); focus(app, *combo);
    key(*combo, Qt::Key_S, Qt::ShiftModifier); combo->showPopup(); pump(app);
    key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked(), "Stereo shortcut escaped a dropdown or popup."); combo->hidePopup(); pump(app, 250);
    // Synthetic popup activation can leave Qt's private container visible.
    if (auto popup = QApplication::activePopupWidget()) popup->hide();
    until(app, [] { return !QApplication::activePopupWidget(); });
    QDialog modal(&scope); modal.setModal(true); modal.show(); pump(app);
    key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked(), "Stereo shortcut escaped a modal dialog."); modal.reject();
    focus(app, scope);
    require(QApplication::focusWidget() == &scope && !QApplication::activeModalWidget() && !QApplication::activePopupWidget(),
        "Stereo check did not restore canvas focus after closing dialogs.");
    key(scope, Qt::Key_F11); pump(app);
    require(scope.presentationFullscreen(), "Stereo check did not enter fullscreen.");
    focus(app, scope); key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(stereo->isChecked(), "Fullscreen Stereo shortcut failed.");
    key(scope, Qt::Key_F11);
    require(player.state().generation == initial.generation && player.state().muteMask == initial.muteMask &&
        player.state().soloChannel == initial.soloChannel && player.state().outputMask == initial.outputMask,
        "Stereo display changes affected playback, Solo or output controls.");
    player.seek(200, false); key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(stereo->isChecked(), "Stereo shortcut changed a pending seek.");
    until(app, [&] { return !player.state().busy; }); window.refresh();
    key(scope, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isChecked() && !player.state().playing, "Paused Stereo shortcut failed or resumed audio.");
    scope.findChild<QPushButton *>("editLayout")->setChecked(false);
    log << "Shift+S: both windows, hidden/fullscreen/paused display, Solo independence and input/repeat/busy guards PASS\n"; log.flush();
}
void checkSolo(QApplication &app, PlayerWindow &window, const QString &directory, QTextStream &log) {
    auto &player = window.player(); auto &scope = window.scopeWindow();
    window.findChild<QTabWidget *>("tabs")->setCurrentIndex(1);
    auto voice = [&](int i) { return window.findChild<QToolButton *>(QString("voice%1").arg(i)); };
    const auto soloKeys = Qt::ControlModifier | Qt::ShiftModifier;
    player.setMuteMask(2); window.refresh();
    const auto generation = player.state().generation;
    const auto visibility = scope.visibleChannels();
    focus(app, *voice(1)); click(*voice(1), Qt::ShiftModifier);
    require(player.state().soloChannel == 1 && player.state().muteMask == ~VoiceMask(2) &&
        voice(1)->isChecked() && voice(1)->property("solo").toBool() && !voice(0)->isChecked(),
        "Shift-click did not solo a previously muted voice.");
    require(player.state().generation == generation && scope.visibleChannels() == visibility,
        "Solo restarted playback or changed the grid.");
    window.grab().save(QDir(directory).filePath("native-solo-player.png"));
    focus(app, scope); key(scope, Qt::Key_3, soloKeys);
    require(player.state().soloChannel == 2 && player.state().muteMask == ~VoiceMask(4),
        "Solo shortcut did not switch to another voice from scopes.");
    key(scope, Qt::Key_3, soloKeys, true);
    key(scope, Qt::Key_9, soloKeys);
    require(player.state().soloChannel == 2, "Auto-repeat or absent channel changed Solo.");
    focus(app, window); key(window, Qt::Key_3, soloKeys | Qt::KeypadModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == 2,
        "Numpad Solo did not restore the original mutes after switching voices.");
    click(*voice(0), Qt::ShiftModifier); click(*voice(0), Qt::ShiftModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == 2,
        "Repeated Shift-click did not restore previous mutes.");
    checkCardSolo(app, window, log);

    // Manual controls take over the visible audible set and clear the Solo marker.
    key(window, Qt::Key_1, soloKeys); click(*voice(1)); window.refresh();
    require(player.state().soloChannel == -1 && player.state().muteMask == ~VoiceMask(3) &&
        !voice(0)->property("solo").toBool(), "Manual channel click retained Solo or lost the audible set.");
    key(window, Qt::Key_2, soloKeys); key(window, Qt::Key_1, Qt::ControlModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == ~VoiceMask(3),
        "Mute shortcut did not leave Solo consistently.");
    for (const QString action : {"enableAllChannels", "disableAllChannels"}) {
        key(window, Qt::Key_1, soloKeys);
        window.findChild<QPushButton *>(action)->click(); window.refresh();
        require(player.state().soloChannel == -1 && player.state().muteMask ==
            (action == "enableAllChannels" ? VoiceMask{} : ~VoiceMask{}), "All-channel action did not cancel Solo.");
    }
    player.setMuteMask(2); key(window, Qt::Key_1, soloKeys);
    key(window, Qt::Key_R, soloKeys);
    require(player.state().soloChannel == -1 && player.state().muteMask == 0,
        "Reset did not clear Solo.");

    player.setMuteMask(2); player.setOutputMask(1); key(window, Qt::Key_1, soloKeys);
    key(window, Qt::Key_F8); until(app, [&] { return !player.state().busy && !player.state().playing; });
    require(player.state().soloChannel == 0, "Pause lost Solo.");
    player.seek(1000, false); player.toggleSolo(2);
    require(player.state().soloChannel == 0, "Solo changed during a pending seek.");
    until(app, [&] { return !player.state().busy; });
    require(player.state().soloChannel == 0 && player.state().muteMask == ~VoiceMask(1), "Seek lost Solo.");
    key(window, Qt::Key_F7); until(app, [&] { return !player.state().busy && player.state().playing; });
    // Worker readiness can precede the next UI refresh; wait for the real button.
    until(app, [&] { return window.findChild<QPushButton *>("stop")->isEnabled(); });
    window.findChild<QPushButton *>("stop")->click(); until(app, [&] { return !player.state().busy; });
    require(player.state().soloChannel == 0 && player.state().positionMs == 0 && !player.state().playing,
        "Stop lost Solo or failed to stop.");
    key(window, Qt::Key_F5); until(app, [&] { return !player.state().busy && player.state().playing; });
    window.loadFile(QDir(directory).filePath("demo.nsf"), 0);
    until(app, [&] { return !player.state().busy; }); window.refresh();
    require(player.state().valid && player.state().info.song == 0 && player.state().soloChannel == 0 &&
        player.state().outputMask == 1, "Subsong change lost Solo/output selection.");
    click(*voice(0), Qt::ShiftModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == 2,
        "Transport/subsong changes lost the original mute selection.");

    // Ordinary field edits, popup/modal use and non-voice indices stay protected.
    auto rows = scope.findChild<QSpinBox *>("gridRows");
    focus(app, *rows); key(*rows, Qt::Key_1, soloKeys);
    require(player.state().soloChannel == -1, "Solo shortcut escaped a numeric field.");
    QDialog dialog(&window); dialog.setModal(true); dialog.show(); pump(app);
    key(window, Qt::Key_1, soloKeys);
    require(player.state().soloChannel == -1, "Solo shortcut escaped a modal dialog."); dialog.close();
    focus(app, window); player.toggleSolo(-1); player.toggleSolo(32); key(window, Qt::Key_0, soloKeys);
    require(player.state().soloChannel == -1, "Invalid source or Full mix was soloed.");
    key(window, Qt::Key_1, soloKeys);
    window.loadFile(QDir(directory).filePath("demo.vgz"));
    until(app, [&] { return !player.state().busy; }); window.refresh();
    require(player.state().valid && player.state().soloChannel == -1 && player.state().muteMask == 0,
        "Another file retained Solo or old mutes.");
    player.setOutputMask(3);
    log << "Solo: Shift-click, both-window/numpad shortcuts, switch/restore, mute/reset takeover, field guards and transport/file lifecycle PASS\n";
    log.flush();
}
}

void checkShortcuts(QApplication &app, const QString &directory, QTextStream &log) {
    checkSelectedCardShortcuts(app, directory, log);
    checkChannelPresentation(log);
    PlayerWindow window; auto &player = window.player(); auto &scope = window.scopeWindow();
    player.setVolume(0); window.show();
    auto help = scope.findChild<QPushButton *>("keyboardShortcuts");
    require(help && !window.findChild<QPushButton *>("keyboardShortcuts"), "Help button must exist only in the visualizer.");
    key(window, Qt::Key_F5); key(window, Qt::Key_9, Qt::ControlModifier);
    key(window, Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier);
    key(window, Qt::Key_0); key(window, Qt::Key_Left, Qt::ControlModifier);
    require(!player.state().valid && !player.state().busy, "Empty-player shortcuts started transport.");
    checkStereoShortcut(app, window, directory, log);
    window.loadFile(QDir(directory).filePath("demo.nsf"), 1);
    until(app, [&] { return !player.state().busy && player.state().positionMs > 200; }); window.refresh();
    auto stereo = scope.findChild<QCheckBox *>("scopeStereo"); stereo->setChecked(true);
    key(window, Qt::Key_S, Qt::ShiftModifier);
    require(!stereo->isEnabled() && stereo->isChecked() && !player.state().info.stereoOutput,
        "Mono source changed its stored stereo preference."); stereo->setChecked(false);
    scope.show(); pump(app); focus(app, window);
    const auto beforeFullscreen = player.state(); const auto windowRect = scope.geometry();
    key(window, Qt::Key_F11); pump(app);
    require(scope.presentationFullscreen(), "F11 from the player did not enter full screen.");
    key(scope, Qt::Key_F11, {}, true); require(scope.presentationFullscreen(), "F11 auto-repeat exited full screen.");
    focus(app, scope); key(scope, Qt::Key_Escape); pump(app);
    require(!scope.presentationFullscreen() && scope.geometry() == windowRect && player.state().playing &&
        player.state().generation == beforeFullscreen.generation, "Full-screen shortcut changed transport or window geometry.");
    log << "F11/Escape: both windows, auto-repeat guard, geometry and uninterrupted playback PASS\n"; log.flush();
    scope.findChild<QPushButton *>("editLayout")->click();
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    auto view = scope.findChild<QComboBox *>("cardView");
    auto item = [&](int id) -> QListWidgetItem * {
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toInt() == id) return list->item(i);
        throw std::runtime_error("Missing shortcut test card.");
    };
    auto kind = [&](int id) { list->setCurrentItem(item(id)); return view->currentIndex(); };
    auto addCard = [&](int source) {
        QTimer::singleShot(0, &scope, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto sources = dialog->findChild<QComboBox *>("newCardSource");
            sources->setCurrentIndex(sources->findData(source));
            dialog->findChild<QComboBox *>("newCardView")->setCurrentIndex(0); dialog->accept();
        });
        scope.findChild<QPushButton *>("addScopeCard")->click();
        const int id = list->currentItem()->data(Qt::UserRole).toInt();
        focus(app, *list); return id;
    };
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

    auto left = window.findChild<QToolButton *>("leftOutput");
    auto right = window.findChild<QToolButton *>("rightOutput");
    key(scope, Qt::Key_Left, Qt::ControlModifier);
    require(player.state().outputMask == 2 && !left->isChecked() && right->isChecked(), "Left shortcut did not synchronize output/button.");
    key(window, Qt::Key_Right, Qt::ControlModifier);
    require(player.state().outputMask == 0 && !left->isChecked() && !right->isChecked(), "Right shortcut did not synchronize output/button.");
    key(window, Qt::Key_Right, Qt::ControlModifier, true);
    key(window, Qt::Key_0, Qt::ControlModifier);
    require(player.state().outputMask == 0 && player.state().muteMask == 2, "Auto-repeat or unassigned Ctrl+0 changed sound.");
    key(window, Qt::Key_Left, Qt::ControlModifier | Qt::KeypadModifier);
    key(scope, Qt::Key_Right, Qt::ControlModifier);
    require(player.state().outputMask == 3 && left->isChecked() && right->isChecked() &&
        player.state().muteMask == 2 && player.state().generation == generation, "Output toggles restarted transport or changed voice mutes.");

    key(scope, Qt::Key_0);
    require(kind(ScopeTheme::FullMix) == 1 && kind(0) == 0 && item(ScopeTheme::FullMix)->checkState() == Qt::Unchecked,
        "Zero changed a voice or revealed hidden Full mix.");
    key(window, Qt::Key_0, Qt::KeypadModifier);
    require(kind(ScopeTheme::FullMix) == 2, "Numpad zero did not cycle Full mix.");
    key(window, Qt::Key_0); require(kind(ScopeTheme::FullMix) == 0, "Full mix cycle did not return to waveform.");
    auto rows = scope.findChild<QSpinBox *>("gridRows"); rows->setValue(8);
    key(window, Qt::Key_0, Qt::AltModifier);
    require(item(ScopeTheme::FullMix)->checkState() == Qt::Checked && scope.outputEnabled(), "Alt+0 did not enable Full mix capture.");
    const int mixDuplicate = addCard(ScopeTheme::FullMix);
    key(scope, Qt::Key_0);
    require(kind(ScopeTheme::FullMix) == 1 && kind(mixDuplicate) == 1, "Full mix duplicates did not cycle together.");
    key(scope, Qt::Key_0, Qt::AltModifier | Qt::KeypadModifier);
    require(item(ScopeTheme::FullMix)->checkState() == Qt::Unchecked && item(mixDuplicate)->checkState() == Qt::Unchecked &&
        !scope.outputEnabled(), "Alt+0 did not hide all Full mix cards and stop capture.");
    require(player.state().muteMask == 2 && player.state().outputMask == 3 && player.state().generation == generation,
        "Full mix shortcuts changed sound or transport.");

    // Reorder through the same item-model signal used by drag/drop.
    require(list->model()->moveRow({}, 0, {}, 3), "Cannot reorder shortcut test cards.");
    key(scope, Qt::Key_1); require(kind(0) == 1 && kind(1) == 0, "Numbers followed grid slots instead of source channels.");
    const int duplicate = addCard(0);
    key(scope, Qt::Key_1);
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
    key(window, Qt::Key_0, Qt::AltModifier);
    require(!scope.isVisible() && item(ScopeTheme::FullMix)->checkState() == Qt::Checked && item(mixDuplicate)->checkState() == Qt::Checked,
        "Hidden-window Full mix shortcut opened the window or failed.");
    scope.show(); focus(app, *list);

    auto input = rows->findChild<QLineEdit *>(); focus(app, *input); input->selectAll();
    key(*input, Qt::Key_1);
    require(input->text() == "1" && kind(0) == 2, "Typing a grid size triggered a view shortcut.");
    key(*input, Qt::Key_Left, Qt::ControlModifier);
    require(player.state().outputMask == 3, "Caret navigation toggled audio output.");
    input->selectAll(); key(*input, Qt::Key_0);
    require(kind(ScopeTheme::FullMix) == 2, "Typing zero triggered a Full mix shortcut.");
    key(*input, Qt::Key_F8); require(!player.state().busy && player.state().playing, "Function key escaped the focused field.");
    focus(app, *view); key(*view, Qt::Key_F2);
    require(kind(0) == 2, "Dropdown intercepted by app shortcut.");
    view->showPopup();
    until(app, [&] { return QApplication::activePopupWidget(); });
    key(window, Qt::Key_2, Qt::ControlModifier);
    key(window, Qt::Key_Right, Qt::ControlModifier); key(window, Qt::Key_0);
    key(window, Qt::Key_2, Qt::ControlModifier | Qt::ShiftModifier);
    require(player.state().soloChannel == -1 && player.state().muteMask == 2 && player.state().outputMask == 3 && kind(ScopeTheme::FullMix) == 2,
        "Shortcut escaped a dropdown popup."); view->hidePopup();
    QDialog other(&window); other.setModal(true); other.show(); pump(app);
    key(window, Qt::Key_2, Qt::ControlModifier);
    key(window, Qt::Key_Right, Qt::ControlModifier); key(window, Qt::Key_0);
    require(player.state().muteMask == 2 && player.state().outputMask == 3 && kind(ScopeTheme::FullMix) == 2,
        "Shortcut escaped a dialog."); other.close();
    focus(app, *list);
    for (int id : {ScopeTheme::FullMix, mixDuplicate}) {
        list->setCurrentItem(item(id)); scope.findChild<QPushButton *>("removeScopeCard")->click();
    }
    const int remaining = list->count(); key(scope, Qt::Key_0); key(window, Qt::Key_0, Qt::AltModifier);
    require(list->count() == remaining && !scope.outputEnabled(), "Full mix shortcut recreated removed cards.");
    log << "Shortcuts: both windows, numpad, stable channel numbers, duplicate/hidden cards, mute/visibility independence, fields/dialog guards PASS\n"; log.flush();
    log << "Shortcuts: Full mix cycle/visibility/capture, duplicates/removal, L/R output toggles and button synchronization PASS\n"; log.flush();

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
    scope.grab().save(QDir(directory).filePath("native-shortcuts-scopes.png"));

    int dialogs = 0; bool guideValid = true;
    auto closeGuide = [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) { guideValid = false; return; }
        auto guide = dialog->findChild<QTextBrowser *>("shortcutsGuide");
        guideValid &= guide && guide->toPlainText().contains("F8") && guide->toPlainText().contains("Reset channels and layout") &&
            guide->toPlainText().contains("Full mix: cycle view") && guide->toPlainText().contains("Toggle left / right audio output") &&
            guide->toPlainText().contains("Solo channel / restore previous channels") && guide->toPlainText().contains("Shift+click") &&
            guide->toPlainText().contains("visualization card") && guide->toPlainText().contains("Shift+S") &&
            guide->toPlainText().contains("Toggle Stereo / Mono display");
        ++dialogs; dialog->grab().save(QDir(directory).filePath("native-shortcuts-help.png")); dialog->accept();
    };
    for (QWidget *target : {static_cast<QWidget *>(&window), static_cast<QWidget *>(&scope)}) {
        QTimer::singleShot(100, &window, closeGuide); key(*target, Qt::Key_F1);
    }
    QTimer::singleShot(100, &window, closeGuide); help->click();
    require(guideValid && dialogs == 3, "Help button/F1 guide did not open.");
    checkSolo(app, window, directory, log);
    require(player.state().outputErrors == 0 && player.state().starvations == 0, "Shortcut check interrupted audio output.");
    window.close();
    log << "Shortcuts: restart/resume/pause, reset, help button/F1 and silent audio continuity PASS\n"; log.flush();
}
