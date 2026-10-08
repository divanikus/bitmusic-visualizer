#include "window.h"
#include "palette.h"
#include "diagnostics.h"
#include "renderersettings.h"
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <cstring>
#include <cstdio>

int runChecks(QApplication &app, const QStringList &arguments);
int runTapChecks(QApplication &app, const QStringList &arguments);
int runGpuChecks(QApplication &app, const QStringList &arguments);
int runGpuSoak(QApplication &app, const QStringList &arguments);

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--software-scopes") == 0) qputenv("BITMUSIC_SOFTWARE_SCOPES", "1");
    QApplication app(argc, argv);
    app.setApplicationName("Bit Music Visualizer");
    app.setApplicationVersion("0.7.2");
    app.setStyle("Fusion");
    app.setPalette(playerPalette());
    app.setWindowIcon(QIcon(":/assets/player.png"));
    const auto arguments = app.arguments();
    // Test runners isolate settings themselves; never inherit a user's saved
    // renderer when exercising the explicitly selected test backend.
    const bool testing = arguments.contains("--tap-checks") || arguments.contains("--gpu-checks") ||
        arguments.contains("--gpu-soak") || arguments.contains("--self-test");
    if (!testing) configureScopeRenderer();
    DiagnosticLog diagnostics(arguments.contains("--diagnostics"));
    if (arguments.contains("--tap-checks")) return runTapChecks(app, arguments);
    if (arguments.contains("--gpu-checks")) return runGpuChecks(app, arguments);
    if (arguments.contains("--gpu-soak")) return runGpuSoak(app, arguments);
    if (arguments.contains("--self-test")) return runChecks(app, arguments);
    PlayerWindow window;
    window.show();
    if (arguments.contains("--diagnostics") && !diagnostics.active())
        QMessageBox::warning(&window, "Diagnostics unavailable", "Could not create the diagnostic log:\n" + diagnostics.path());
    if (diagnostics.active()) {
        for (const auto screen : app.screens())
            qInfo().noquote() << "screen=" << screen->name() << "geometry=" << screen->geometry()
                << "dpr=" << screen->devicePixelRatio() << "refresh=" << screen->refreshRate();
        auto timer = new QTimer(&window); timer->setInterval(2000);
        QObject::connect(timer, &QTimer::timeout, &window, [&window] {
            const auto state = window.player().state(); const auto frame = window.player().scopes();
            auto &scope = window.scopeWindow();
            const auto gpu = dynamic_cast<ScopeGpu *>(scope.findChild<QQuickWidget *>("scopeGpu"));
            const auto screen = scope.screen();
            qInfo().noquote() << "state audioMs=" << state.positionMs << "scopeMs=" << frame.positionMs
                << "generation=" << state.generation << '/' << frame.generation
                << "playing=" << state.playing << "busy=" << state.busy
                << "voices=" << state.info.voices.size() << "readyMask=" << frame.mask
                << "scopeBlocks=" << state.scopeBlocks << "visible=" << scope.renderingVisible()
                << "suspended=" << state.scopesSuspended << "audioErrors=" << state.starvations << '/' << state.outputErrors
                << "backend=" << (gpu ? gpu->backend() : "CPU")
                << "targetFps=" << scope.frameRate()
                << "gpuFrames=" << (gpu ? gpu->renderedFrames() : 0)
                << "completed=" << (gpu ? gpu->completedFrames() : 0)
                << "history=" << (gpu ? gpu->historySize() : 0)
                << "window=" << scope.size() << "dpr=" << scope.devicePixelRatioF()
                << "screen=" << (screen ? screen->name() : QString())
                << "error=" << state.error << "scopeError=" << state.scopeError;
        });
        timer->start();
    }
    auto files = arguments.mid(1); files.removeAll("--diagnostics"); files.removeAll("--software-scopes");
    if (!files.isEmpty()) window.loadFile(files.front());
    return app.exec();
}
