#include "engine.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QtEndian>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pump(QApplication &app, int ms) {
    QEventLoop loop(&app);
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
void until(QApplication &app, const std::function<bool()> &ready, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000) { app.processEvents(); QThread::msleep(4); }
    require(ready(), message);
}
void checkControlChanges(QApplication &app, const QString &path) {
    NativePlayer player;
    player.setVolume(0);
    player.setScopeMask(3);
    player.setScopesEnabled(true);
    player.load(path);
    until(app, [&] { return player.state().valid && !player.state().busy; }, "Capture fixture did not load.");
    player.seek(250, false);
    until(app, [&] { const auto state = player.state(); const auto frame = player.scopes();
        return !state.busy && frame.generation == state.generation && frame.positionMs == 250 && frame.mask == VoiceMask(3);
    }, "Capture fixture did not prepare.");
    // Join the worker so each control's immediate effect can be checked without
    // racing a replacement publication. The player and its snapshots stay alive.
    player.shutdown();
    const auto first = player.scopes();
    player.setScopesEnabled(true);
    player.setScopeMask(3);
    const auto same = player.scopes();
    require(same.generation == first.generation && same.positionMs == first.positionMs && same.mask == first.mask &&
            same.channels == first.channels && same.left == first.left && same.right == first.right,
            "Idempotent capture controls cleared a published snapshot.");
    player.setScopeMask(1);
    const auto reduced = player.scopes();
    require(reduced.mask == VoiceMask(1) && reduced.channels[0] == first.channels[0] && reduced.left[0] == first.left[0] &&
            reduced.right[0] == first.right[0] && reduced.channels[1].isEmpty() && reduced.left[1].isEmpty() && reduced.right[1].isEmpty() &&
            reduced.generation == first.generation && reduced.positionMs == first.positionMs && reduced.noteHz == first.noteHz &&
            reduced.instruments == first.instruments, "Mask reduction changed retained PCM or snapshot metadata.");
    player.setScopeMask(3);
    require(player.scopes().mask == VoiceMask(1) && player.scopes().channels[1].isEmpty(), "Added capture bit fabricated readiness.");
    player.setScopeMask(0);
    require(!player.scopes().mask && player.scopes().channels.size() == first.channels.size() &&
            player.scopes().generation == first.generation, "Empty mask discarded snapshot identity/shape.");
    player.setScopesEnabled(false);
    require(!player.scopes().generation && player.scopes().channels.isEmpty() && player.scopes().noteHz.isEmpty(),
            "Visibility change did not reset the whole snapshot.");
    player.setScopesEnabled(true);
    require(!player.scopes().generation && player.scopes().channels.isEmpty(), "Reopening capture restored old data.");
}
void checkOutputControls(QApplication &app, const QString &path) {
    NativePlayer player;
    player.setVolume(0);
    player.setScopeMask(0);
    player.setOutputScopesEnabled(true);
    player.load(path);
    until(app, [&] { return player.outputScopes().sequence && player.state().positionMs >= 100; }, "Output capture did not start.");
    player.seek(player.state().positionMs, false);
    until(app, [&] { return !player.state().busy; }, "Output capture did not pause.");
    const auto frozen = player.outputScopes();
    require(frozen.sequence && frozen.generation == player.state().generation, "Pause lost delivered output.");
    player.setOutputScopesEnabled(true);
    player.setScopesEnabled(true);
    player.setScopeMask(0);
    player.setScopesEnabled(false);
    pump(app, 60);
    auto frame = player.outputScopes();
    require(frame.token == frozen.token && frame.sequence == frozen.sequence && frame.left == frozen.left && frame.right == frozen.right,
            "Chip controls or redundant output enable invalidated frozen output.");
    player.setOutputScopesEnabled(false);
    require(!player.outputScopes().sequence, "Disabled output exposed a frozen snapshot.");
    player.setOutputScopesEnabled(false);
    player.setOutputScopesEnabled(true);
    require(!player.outputScopes().sequence, "New output token admitted old paused history.");
    player.seek(250, true);
    until(app, [&] { return player.outputScopes().sequence; }, "Output capture did not resume.");
    frame = player.outputScopes();
    require(frame.token != frozen.token && frame.generation == player.state().generation, "Resumed output used an old identity.");
    player.seek(3600000, true);
    require(!player.outputScopes().sequence, "Busy seek exposed old output history.");
    player.seek(500, false);
    until(app, [&] { return !player.state().busy; }, "Paused replacement seek stuck.");
    require(!player.outputScopes().sequence, "Paused replacement seek retained delivered output.");
    player.seek(250, true);
    until(app, [&] { return player.outputScopes().sequence; }, "Output did not recover after replacement seek.");
    player.unload();
    until(app, [&] { return !player.state().busy; }, "Capture unload stuck.");
    require(!player.outputScopes().sequence, "Unload exposed prior output history.");
    require(!player.state().outputErrors, "Capture changes caused audio output errors.");
}
void checkEarlyUnload(QApplication &app, const QString &directory, QTextStream &log) {
    QTemporaryDir files(QDir(directory).absoluteFilePath("capture-unload-XXXXXX"));
    QFile original(QDir(directory).filePath("named.nsfe"));
    require(files.isValid() && original.open(QIODevice::ReadOnly), "Cannot read early-unload fixture.");
    auto bytes = original.readAll();
    const auto time = bytes.indexOf("time");
    require(time >= 0 && time + 8 <= bytes.size(), "NSFE fixture has no duration chunk.");
    // A short authored tune leaves the delivered clock inside the 30 ms pause
    // tolerance even when the device requests a larger callback block.
    qToLittleEndian<qint32>(20, bytes.data() + time + 4);
    const auto path = files.filePath("short.nsfe");
    QFile copy(path);
    require(copy.open(QIODevice::WriteOnly) && copy.write(bytes) == bytes.size(), "Cannot write early-unload fixture.");
    copy.close();
    NativePlayer player;
    player.setVolume(0);
    player.setAutoSubsong(false);
    player.setOutputScopesEnabled(true);
    for (int attempt = 0; attempt < 10; ++attempt) {
        player.load(path);
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 1000) {
            const auto frame = player.outputScopes();
            const auto state = player.state();
            if (!state.busy && state.playing && frame.sequence && frame.generation == state.generation && state.positionMs <= 30) {
                player.unload();
                until(app, [&] { return !player.state().busy; }, "Early unload remained busy.");
                log << "Unload inside pause tolerance: playhead=" << state.positionMs << " ms; retained output sequence="
                    << player.outputScopes().sequence << '\n';
                log.flush();
                require(!player.outputScopes().sequence, "Early unload froze prior output as if it were a pause.");
                return;
            }
            if (!state.busy && state.finished) break;
            app.processEvents(); QThread::usleep(100);
        }
    }
    throw std::runtime_error("Could not observe the short tune before natural completion.");
}
}

void checkCaptureControls(QApplication &app, const QString &directory, QTextStream &log) {
    const auto path = QDir(directory).filePath("demo.nsf");
    checkControlChanges(app, path);
    checkOutputControls(app, path);
    checkEarlyUnload(app, directory, log);
    log << "Capture controls: idempotence, retained/hidden rows, visibility reset, chip/output independence, "
           "paused token invalidation, busy/paused seeks and unload PASS\n";
    log.flush();
}
