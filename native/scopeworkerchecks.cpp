#include "engine.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
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
    while (!ready() && timer.elapsed() < 15000) { app.processEvents(); QThread::msleep(4); }
    require(ready(), message);
}
bool ready(NativePlayer &player, VoiceMask mask, int position) {
    const auto state = player.state(); const auto frame = player.scopes();
    return state.valid && !state.busy && state.error.isEmpty() && state.scopeError.isEmpty() &&
           frame.generation == state.generation && frame.mask == mask && frame.positionMs == position;
}
void checkPreparation(QApplication &app, const QString &path, bool shared) {
    NativePlayer player;
    player.setVolume(0);
    player.setAutoSubsong(false);
    player.setScopeMask(1);
    player.setScopesEnabled(true);
    player.load(path);
    until(app, [&] { return !player.state().busy && player.state().valid; }, "Scope preparation fixture failed to load.");
    player.seek(250, false);
    until(app, [&] { return ready(player, 1, 250); }, "Single scope did not prepare.");
    require(player.state().sharedScopes == shared, "Scope fixture used an unexpected capture path.");
    const auto first = player.scopes();
    const QVector<float> frozen(first.channels[0].cbegin(), first.channels[0].cend());
    require(first.channels.size() == player.state().info.voices.size() && first.left[0].size() == ScopeFrames &&
            first.right[0].size() == ScopeFrames, "Scope snapshot shape changed.");
    for (int i = 1; i < first.channels.size(); ++i) {
        require(first.channels[i].isEmpty() && first.left[i].isEmpty() && first.right[i].isEmpty(), "Hidden initial scope retained PCM.");
        require(shared ? first.noteHz[i].isEmpty() : first.noteHz[i] == QVector<float>{-1}, "Unavailable scope pitch convention changed.");
    }
    player.setScopeMask(3);
    until(app, [&] { return ready(player, 3, 250); }, "Added scope did not prepare.");
    require(player.scopes().channels[0] == first.channels[0] && player.scopes().left[0] == first.left[0] &&
            player.scopes().right[0] == first.right[0], "Preparing a new channel changed a paused channel's history.");
    const auto stable = player.state();
    player.setScopeFrameRate(120); pump(app, 40);
    player.setScopeFrameRate(30); pump(app, 80);
    require(player.state().generation == stable.generation && player.state().scopeBlocks == stable.scopeBlocks &&
            player.scopes().channels[0] == first.channels[0], "FPS change invalidated paused preparation.");
    player.setScopeMask(0);
    until(app, [&] { return player.state().scopesSuspended; }, "Empty scope mask did not suspend.");
    const auto suspended = player.state(); pump(app, 80);
    require(player.state().scopeBlocks == suspended.scopeBlocks && !player.scopes().mask, "Empty scope mask kept rendering.");
    player.setScopeMask(3);
    until(app, [&] { return ready(player, 3, 250); }, "Scope mask resume failed.");

    player.seek(60000, false);
    until(app, [&] { return !player.state().busy; }, "Distant paused seek remained busy.");
    pump(app, 20);
    player.setScopeMask(1);
    player.setScopeFrameRate(60);
    player.seek(250, false);
    player.setScopesEnabled(false);
    until(app, [&] { return !player.state().busy && player.state().scopesSuspended; }, "Scope preparation cancellation stuck.");
    require(player.scopes().channels.isEmpty(), "Hidden scope snapshot was not cleared.");
    player.setScopesEnabled(true);
    until(app, [&] { return ready(player, 1, 250); }, "Newest paused snapshot did not replace cancelled work.");
    pump(app, 100);
    const auto frame = player.scopes();
    require(first.channels[0] == frozen, "Worker mutated an already published snapshot.");
    require(!player.state().playing && frame.mask == VoiceMask(1) && frame.positionMs == 250 &&
            frame.generation == player.state().generation, "Cancelled preparation published stale data.");
    for (int i = 1; i < frame.channels.size(); ++i)
        require(frame.channels[i].isEmpty() && frame.left[i].isEmpty() && frame.right[i].isEmpty(), "Hidden scope retained PCM.");
    require(player.state().starvations == stable.starvations && player.state().outputErrors == stable.outputErrors,
            "Scope changes interrupted audio output.");
}
void checkFailure(QApplication &app, const QString &original, const QString &copy, bool shared) {
    require(QFile::copy(original, copy), "Cannot copy scope error fixture.");
    NativePlayer player;
    player.setVolume(0);
    player.setAutoSubsong(false);
    player.setScopeMask(1);
    player.load(copy);
    until(app, [&] { return !player.state().busy && player.state().playing; }, "Scope error fixture failed to load.");
    if (!shared) {
        player.setScopesEnabled(true);
        until(app, [&] { const auto s = player.state(); const auto f = player.scopes();
            return f.generation == s.generation && f.mask == VoiceMask(1);
        }, "Scope error fixture did not prepare its first voice.");
    }
    require(QFile::remove(copy), "Cannot remove private scope error fixture.");
    QString expected;
    try { openMusicTrack(copy); }
    catch (const std::exception &error) { expected = QString::fromUtf8(error.what()); }
    require(!expected.isEmpty(), "Scope error fixture did not fail.");
    // Shared capture fails while opening its decoder; fallback capture fails
    // inside a preparation task when adding the second channel.
    if (shared) player.setScopesEnabled(true); else player.setScopeMask(3);
    until(app, [&] { return !player.state().scopeError.isEmpty(); }, "Scope decoder error did not propagate.");
    auto state = player.state();
    require(state.valid && state.playing && !state.busy && state.error.isEmpty() && state.scopeError == expected,
            "Scope failure changed primary audio state or error text.");
    require(QFile::copy(original, copy), "Cannot restore private scope error fixture.");
    player.seek(250, false);
    until(app, [&] { return ready(player, shared ? 1 : 3, 250); }, "Scope failure did not recover on a new command.");
    require(player.state().sharedScopes == shared && player.state().error.isEmpty(), "Scope recovery changed capture path/audio error.");
}
}

void checkScopeWorker(QApplication &app, const QString &directory, QTextStream &log) {
    QTemporaryDir files(QDir(directory).absoluteFilePath("scope-worker-XXXXXX"));
    require(files.isValid(), "Cannot isolate scope worker checks.");
    for (const auto &extension : {QString("nsf"), QString("spc")}) {
        const auto path = QDir(directory).filePath("demo." + extension);
        const bool shared = extension == "spc";
        checkPreparation(app, path, shared);
        checkFailure(app, path, files.filePath("copy." + extension), shared);
        log << "Scope worker " << extension << ": mask/FPS/history, suspension, seek/revision cancellation, "
               "snapshot shape, decoder/task error isolation and recovery PASS\n";
        log.flush();
    }
}
