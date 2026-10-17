#include "engine.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
void pump(QApplication &app, int milliseconds) {
    QEventLoop loop(&app);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
void until(QApplication &app, const std::function<bool()> &condition, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 10000) { app.processEvents(); QThread::msleep(4); }
    require(condition(), message);
}
}

void checkEngineCommands(QApplication &app, const QString &directory, QTextStream &log) {
    const auto music = QDir(directory).filePath("demo.nsf"); // Unknown duration: a long seek really needs cancellation.
    QTemporaryDir files(QDir(directory).absoluteFilePath("engine-errors-XXXXXX"));
    require(files.isValid(), "Cannot isolate engine error checks.");
    const auto missing = files.filePath("missing.nsf");
    QString missingError;
    try { openMusicTrack(missing); }
    catch (const std::exception &error) { missingError = QString::fromUtf8(error.what()); }
    require(!missingError.isEmpty(), "Missing-file fixture did not fail.");

    NativePlayer player;
    player.setVolume(0);
    player.setAutoSubsong(false);
    player.seek(0, false);
    until(app, [&] { return !player.state().busy; }, "Empty seek remained busy.");
    auto state = player.state();
    require(!state.valid && !state.playing && !state.finished && state.error == "Open a music file first.",
            "Empty seek error/state changed.");
    player.load(music);
    until(app, [&] { auto s = player.state(); return !s.busy && s.valid && s.playing; }, "Load after empty seek failed.");
    player.seek(333, false);
    until(app, [&] { return !player.state().busy; }, "Paused seek remained busy.");
    state = player.state();
    require(state.valid && !state.playing && !state.finished && state.error.isEmpty() && state.positionMs == 332,
            "Paused seek lost its sample-rounded position or transport state.");
    player.seek(-100, false);
    until(app, [&] { return !player.state().busy; }, "Clamped seek remained busy.");
    require(player.state().positionMs == 0, "Negative seek did not clamp to zero.");

    player.load(missing);
    until(app, [&] { return !player.state().busy; }, "Failed load remained busy.");
    state = player.state();
    require(!state.valid && !state.playing && !state.finished && state.error == missingError && state.scopeError.isEmpty(),
            "Load failure lost its original error or transport state.");
    // Existing presentation retains old metadata until a successful load or unload.
    require(state.info.path == music, "Failed load unexpectedly discarded published metadata.");
    player.seek(0, false);
    until(app, [&] { return !player.state().busy; }, "Seek after failed load remained busy.");
    require(player.state().error == "Open a music file first.", "Failed load retained a usable decoder.");
    player.load(music);
    require(player.state().error.isEmpty() && !player.state().finished, "New command did not clear its old error/finish state.");
    until(app, [&] { return !player.state().busy && player.state().playing; }, "Recovery load failed.");
    player.setScopesEnabled(true);
    player.setScopeMask(1);
    player.seek(3600000, true);
    pump(app, 20);
    player.seek(250, false);
    const auto seekGeneration = player.state().generation;
    until(app, [&] {
        const auto s = player.state(); const auto f = player.scopes();
        return !s.busy && f.generation == s.generation && f.positionMs == 250 && f.mask == VoiceMask(1);
    }, "New paused seek/scopes did not supersede long work.");
    pump(app, 100);
    state = player.state();
    require(state.generation == seekGeneration && state.valid && !state.playing && !state.finished && state.positionMs == 250 &&
            state.error.isEmpty() && state.scopeError.isEmpty(), "Cancelled seek published stale state or an error.");

    player.seek(3600000, true);
    pump(app, 20);
    player.load(missing);
    until(app, [&] { return !player.state().busy; }, "Failed replacement did not cancel a long seek.");
    require(!player.state().valid && player.state().error == missingError, "Cancelled work overrode replacement failure.");
    player.load(missing);
    player.load(music);
    until(app, [&] { return !player.state().busy && player.state().playing; }, "New load did not supersede a failing load.");
    require(player.state().valid && player.state().error.isEmpty(), "Stale load error overrode recovery.");

    player.seek(3600000, true);
    pump(app, 20);
    player.unload();
    const auto unloadGeneration = player.state().generation;
    until(app, [&] { return !player.state().busy; }, "Unload did not cancel a long seek.");
    pump(app, 100);
    state = player.state();
    require(state.generation == unloadGeneration && !state.valid && !state.playing && !state.finished &&
            state.info.path.isEmpty() && state.positionMs == 0 && state.playbackEndMs == -1 && !state.durationLimited &&
            state.error.isEmpty() && state.scopeError.isEmpty(), "Unload did not retain its empty state.");
    player.seek(0, false);
    until(app, [&] { return !player.state().busy; }, "Seek after unload remained busy.");
    require(player.state().error == "Open a music file first.", "Unload retained a decoder.");
    player.load(music);
    until(app, [&] { return !player.state().busy && player.state().playing; }, "Load after unload failed.");
    player.seek(3600000, true);
    pump(app, 20);
    QElapsedTimer shutdown;
    shutdown.start();
    player.shutdown();
    require(!player.isRunning() && shutdown.elapsed() < 750, "Shutdown did not cancel long engine work promptly.");
    log << "Engine commands: empty seek, exact load error, retained metadata, recovery, paused/clamped seek, "
           "superseding seek/load/unload and shutdown cancellation PASS\n";
    log.flush();
}
