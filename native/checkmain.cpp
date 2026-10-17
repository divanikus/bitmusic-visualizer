// Developer-only entry point. This file and all checks stay out of the player.
#include "window.h"
#include "palette.h"
#include "diagnostics.h"
#include <QApplication>
#include <QIcon>
#include <cstring>
#include <cstdio>

int runChecks(QApplication &app, const QStringList &arguments);
int runTapChecks(QApplication &app, const QStringList &arguments);
int runGpuChecks(QApplication &app, const QStringList &arguments);
int runGpuSoak(QApplication &app, const QStringList &arguments);
int runMidiChecks(QApplication &app, const QStringList &arguments);
int runMt32Checks(QApplication &app, const QStringList &arguments);
int runTrackerChecks(QApplication &app, const QStringList &arguments);
int runPlaylistChecks(QApplication &app, const QStringList &arguments);
int runTttChecks(QApplication &app, const QStringList &arguments);
int runMusicViewChecks(QApplication &app, const QStringList &arguments);

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--software-scopes") == 0) qputenv("BITMUSIC_SOFTWARE_SCOPES", "1");
    QApplication app(argc, argv);
    app.setApplicationName("Bit Music Visualizer");
    app.setApplicationVersion(QStringLiteral(BITMUSIC_VERSION));
    app.setStyle("Fusion");
    app.setPalette(playerPalette());
    app.setWindowIcon(QIcon(":/assets/player.png"));
    const auto arguments = app.arguments();
    app.setQuitOnLastWindowClosed(false);
    // Each suite isolates its settings; never select a user's saved renderer.
    DiagnosticLog diagnostics(arguments.contains("--diagnostics"));
    if (arguments.contains("--tap-checks")) return runTapChecks(app, arguments);
    if (arguments.contains("--midi-checks")) return runMidiChecks(app, arguments);
    if (arguments.contains("--mt32-checks")) return runMt32Checks(app, arguments);
    if (arguments.contains("--tracker-checks")) return runTrackerChecks(app, arguments);
    if (arguments.contains("--playlist-checks")) return runPlaylistChecks(app, arguments);
    if (arguments.contains("--ttt-checks")) return runTttChecks(app, arguments);
    if (arguments.contains("--music-view-checks")) return runMusicViewChecks(app, arguments);
    if (arguments.contains("--gpu-checks")) return runGpuChecks(app, arguments);
    if (arguments.contains("--gpu-soak")) return runGpuSoak(app, arguments);
    if (arguments.contains("--self-test")) return runChecks(app, arguments);
    std::fprintf(stderr, "Usage: bitmusic_checks <suite> <fixture directory> [options]\n"
        "Suites: --self-test, --tap-checks, --gpu-checks, --gpu-soak, --midi-checks,\n"
        "        --mt32-checks, --tracker-checks, --ttt-checks, --playlist-checks, --music-view-checks\n");
    return 2;
}
