#include "window.h"
#include "appsettings.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pump(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
void until(const std::function<bool()> &condition, const char *message) {
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < 5000) pump(10);
    require(condition(), message);
}
void edit(ScopeWindow &scope, const std::function<void(QDialog *)> &action) {
    scope.show(); scope.findChild<QPushButton *>("editLayout")->setChecked(true);
    bool opened = false; std::exception_ptr error;
    QTimer::singleShot(30, &scope, [&] {
        auto dialog = scope.findChild<QDialog *>("scopeWavesDialog"); if (!dialog) return;
        opened = true;
        try { action(dialog); } catch (...) { error = std::current_exception(); }
        dialog->reject();
    });
    scope.findChild<QPushButton *>("scopeWaves")->click();
    require(opened, "Frame rate dialog did not open."); if (error) std::rethrow_exception(error);
    scope.findChild<QPushButton *>("editLayout")->setChecked(false);
}
void selectRate(ScopeWindow &scope, int fps) {
    edit(scope, [&](QDialog *dialog) {
        auto combo = dialog->findChild<QComboBox *>("waveFrameRate");
        require(combo && combo->count() == 3 && combo->findData(fps) >= 0, "FPS choices missing.");
        combo->setCurrentIndex(combo->findData(fps));
        require(scope.frameRate() == fps, "FPS is not applied live.");
    });
}
void preferences() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat());
    settings.remove("Waveforms/FrameRate"); settings.setValue("Other/PacingKeep", 42); settings.sync();
    {
        ScopeWindow scope; require(scope.frameRate() == 30, "Default FPS is not 30.");
        selectRate(scope, 120);
    }
    {
        ScopeWindow scope; require(scope.frameRate() == 120, "FPS did not survive restart.");
        selectRate(scope, 60);
        scope.beginFile(); scope.findChild<QPushButton *>("resetLayout")->click();
        scope.findChild<QComboBox *>("scopeTheme")->setCurrentIndex(0);
        require(scope.frameRate() == 60, "File, layout or theme reset changed FPS.");
    }
    {
        ScopeWindow scope; require(scope.frameRate() == 60, "60 FPS did not survive restart.");
        edit(scope, [&](QDialog *dialog) {
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
            require(scope.frameRate() == 30, "Waveform defaults did not restore 30 FPS.");
        });
    }
    settings.sync();
    require(settings.value("Waveforms/FrameRate").toInt() == 30 && settings.value("Other/PacingKeep").toInt() == 42,
        "Saving FPS lost defaults or unrelated preferences.");
    for (const QVariant &value : {QVariant("bad"), QVariant(0), QVariant(75), QVariant(-1)}) {
        settings.setValue("Waveforms/FrameRate", value); settings.sync();
        ScopeWindow scope; require(scope.frameRate() == 30, "Invalid FPS did not fall back to 30.");
    }
    settings.setValue("Waveforms/FrameRate", 30); settings.sync();
}
void live(const QString &path, QTextStream &log) {
    PlayerWindow window; window.player().setVolume(0); window.show(); window.scopeWindow().resize(640, 480);
    auto &scope = window.scopeWindow();
    auto &player = window.player();
    int requests = 0;
    auto refresh = scope.onRefreshRequested;
    scope.onRefreshRequested = [&] { ++requests; refresh(); };
    window.loadFile(path);
    until([&] { return player.scopes().mask != 0 && player.state().positionMs > 300; }, "FPS fixture was not ready.");
    const auto generation = player.state().generation;
    for (int fps : {30, 60, 120, 30}) {
        selectRate(scope, fps);
        require(player.scopeFrameRate() == fps && player.state().generation == generation && player.scopes().mask,
            "FPS change restarted playback or invalidated scopes.");
        pump(100);
        int snapshots = 0, previous = -1;
        QTimer poll; poll.setTimerType(Qt::PreciseTimer); poll.setInterval(2);
        QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
            const auto frame = player.scopes();
            if (frame.mask && frame.positionMs != previous) { previous = frame.positionMs; ++snapshots; }
        });
        const int before = requests;
        QElapsedTimer clock; clock.start(); poll.start(); pump(1100); poll.stop();
        const auto elapsed = clock.elapsed(); const auto state = player.state();
        log << QFileInfo(path).suffix() << " target=" << fps << " requests/s=" << (requests-before)*1000./elapsed
            << " fresh snapshots/s=" << snapshots*1000./elapsed << " audio errors=" << state.starvations << '/' << state.outputErrors << '\n'; log.flush();
        require(requests > before && snapshots > 5 && state.error.isEmpty() && state.scopeError.isEmpty() &&
            !state.starvations && !state.outputErrors, "Live FPS change stalled scopes or audio.");
    }
    selectRate(scope, 120);
    for (bool minimize : {false, true}) {
        if (minimize) scope.showMinimized(); else scope.hide();
        until([&] { return player.state().scopesSuspended; }, "Hidden scopes did not suspend capture.");
        const int before = requests; const auto blocks = player.state().scopeBlocks;
        pump(150);
        require(requests == before && player.state().scopeBlocks == blocks, "Hidden/minimized scopes kept ticking.");
        scope.showNormal();
        until([&] { return requests > before && player.scopes().mask; }, "Restored scopes did not resume.");
        require(scope.frameRate() == 120 && player.scopeFrameRate() == 120, "Restore changed FPS.");
    }
    player.seek(500, true);
    until([&] { auto f = player.scopes(); return f.mask && f.generation == player.state().generation; }, "Seek at 120 FPS did not recover.");
    window.loadFile(path);
    until([&] { return !player.state().busy && player.scopes().mask; }, "Reload at 120 FPS did not recover.");
    require(scope.frameRate() == 120 && player.scopeFrameRate() == 120, "Loading a file lost FPS.");
    selectRate(scope, 30);
    // Do not retain references to counters while the windows are being destroyed.
    scope.onRefreshRequested = refresh;
    window.close();
}
}
void checkFrameRates(QApplication &, const QString &directory, QTextStream &log) {
    preferences();
    log << "Frame rate preferences: live selection, restart, defaults, invalid values, file/layout/theme independence PASS\n"; log.flush();
    for (const auto extension : {"nsf", "vgz", "spc"}) live(QDir(directory).filePath(QString("demo.%1").arg(extension)), log);
    log << "Frame rate playback: 30/60/120/30, hidden/minimized suspension, restore, seek/reload, audio errors=0 PASS\n"; log.flush();
}
