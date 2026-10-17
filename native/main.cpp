#include "playerwindow.h"
#include "palette.h"
#include "diagnostics.h"
#include "renderersettings.h"
#include "romfiles.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QScreen>
#include <QWindow>
#include <QTimer>
#include <cstring>
#include <cstdio>

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
    QCommandLineParser parser;
    parser.setApplicationDescription("Retro music player with channel visualizations.");
    parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"diagnostics","Write a local diagnostic log."});
    parser.addOption({"software-scopes","Use CPU visualization rendering."});
    parser.addPositionalArgument("files","Music files or playlists to open.","[files...]");
    if(!parser.parse(arguments)) {std::fprintf(stderr,"%s\n",qPrintable(parser.errorText()));return 2;}
    if(parser.isSet("help")) {std::printf("%s",qPrintable(parser.helpText()));return 0;}
    if(parser.isSet("version")) {std::printf("%s %s\n",qPrintable(app.applicationName()),qPrintable(app.applicationVersion()));return 0;}
    configureScopeRenderer();
    DiagnosticLog diagnostics(parser.isSet("diagnostics"));
    QDir().mkpath(QDir(playerRomsDirectory()).filePath("c64"));
    QDir().mkpath(QDir(playerRomsDirectory()).filePath("ymfm"));
    QDir().mkpath(QDir(playerRomsDirectory()).filePath("mt32"));
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
    const auto files = parser.positionalArguments();
    if (files.size()==1) window.loadFile(files.front());
    else if(!files.isEmpty()) window.addFiles(files,true);
    return app.exec();
}
