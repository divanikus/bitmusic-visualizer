#include "window.h"
#include "appsettings.h"
#include "renderersettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>
#include <QSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void pump(QApplication &app, int ms = 60) {
    QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
int brightness(const QImage &image, const QRectF &region) {
    const double dpr = image.devicePixelRatio();
    const QRect pixels(qRound(region.x()*dpr), qRound(region.y()*dpr), qRound(region.width()*dpr), qRound(region.height()*dpr));
    int value = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x) value += image.pixelColor(x,y).green();
    return value;
}
class TimedScopes : public ScopeWindow {
public:
    QVector<double> paints;
protected:
    void paintEvent(QPaintEvent *event) override {
        QElapsedTimer time; time.start(); ScopeWindow::paintEvent(event); paints.push_back(time.nsecsElapsed()/1e6);
    }
};
void checkEffectSettings(QApplication &app, QTextStream &log) {
    auto edit = [&](ScopeWindow &scope, const std::function<void(QDialog *)> &action) {
        scope.show(); scope.findChild<QPushButton *>("editLayout")->setChecked(true); pump(app);
        std::exception_ptr failure; bool opened = false;
        QTimer::singleShot(30, &scope, [&] {
            auto dialog = scope.findChild<QDialog *>("scopeEffectsDialog");
            if (!dialog) return;
            opened = true;
            try { action(dialog); } catch (...) { failure = std::current_exception(); }
            dialog->reject(); // Escape / title-bar X retain live changes too.
        });
        scope.findChild<QPushButton *>("scopeEffects")->click();
        require(opened, "Effects dialog did not open.");
        if (failure) std::rethrow_exception(failure);
    };
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat); ini.setValue("Other/Keep", "untouched"); ini.sync(); }
    {
        ScopeWindow first;
        edit(first, [&](QDialog *dialog) {
            dialog->findChild<QCheckBox *>("effectTrail")->setChecked(false);
            dialog->findChild<QCheckBox *>("effectGlow")->setChecked(false);
            dialog->findChild<QSpinBox *>("effectDuration")->setValue(999);
            dialog->findChild<QSpinBox *>("effectTrailStrength")->setValue(81);
            dialog->findChild<QSpinBox *>("effectGlowStrength")->setValue(100);
        });
    }
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
      require(ini.value("Effects/TrailDurationMs").toInt() == 999 && ini.value("Effects/GlowBrightness").toInt() == 100 &&
          ini.value("Other/Keep").toString() == "untouched", "Portable effects save lost values or unrelated keys."); }
    {
        ScopeWindow restarted;
        auto theme = restarted.findChild<QComboBox *>("scopeTheme");
        QMetaObject::invokeMethod(theme, "activated", Q_ARG(int, 0)); // Theme save must retain effects.
        edit(restarted, [&](QDialog *dialog) {
            require(!dialog->findChild<QCheckBox *>("effectTrail")->isChecked() && !dialog->findChild<QCheckBox *>("effectGlow")->isChecked() &&
                dialog->findChild<QSpinBox *>("effectDuration")->value() == 999 && dialog->findChild<QSpinBox *>("effectTrailStrength")->value() == 81 &&
                dialog->findChild<QSpinBox *>("effectGlowStrength")->value() == 100, "Restart did not restore all five effects settings.");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
        });
    }
    {
        ScopeWindow defaults;
        edit(defaults, [&](QDialog *dialog) {
            require(dialog->findChild<QCheckBox *>("effectTrail")->isChecked() && dialog->findChild<QCheckBox *>("effectGlow")->isChecked() &&
                dialog->findChild<QSpinBox *>("effectDuration")->value() == 180 && dialog->findChild<QSpinBox *>("effectTrailStrength")->value() == 35 &&
                dialog->findChild<QSpinBox *>("effectGlowStrength")->value() == 18, "Restore Defaults was not persisted.");
        });
    }
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
      ini.setValue("Effects/TrailDurationMs", 100000); ini.setValue("Effects/TrailBrightness", -5);
      ini.setValue("Effects/GlowBrightness", "bad"); ini.setValue("Effects/TrailEnabled", "bad"); ini.sync(); }
    {
        ScopeWindow invalid;
        edit(invalid, [&](QDialog *dialog) {
            require(dialog->findChild<QSpinBox *>("effectDuration")->value() == 999 && dialog->findChild<QSpinBox *>("effectTrailStrength")->value() == 0 &&
                dialog->findChild<QSpinBox *>("effectGlowStrength")->value() == 18 && dialog->findChild<QCheckBox *>("effectTrail")->isChecked(),
                "Invalid effects settings did not recover safely.");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
        });
    }
    // A file used as a directory forces a write error even under administrator rights.
    const auto path = playerSettingsPath();
    app.setProperty("settingsFileForTests", path + "/blocked.ini");
    {
        ScopeWindow blocked; bool warned = false;
        QTimer dismiss;
        QObject::connect(&dismiss, &QTimer::timeout, &blocked, [&] {
            if (auto message = blocked.findChild<QMessageBox *>()) { warned = message->text().contains(path); message->accept(); }
        });
        dismiss.start(20);
        edit(blocked, [&](QDialog *dialog) { dialog->findChild<QSpinBox *>("effectDuration")->setValue(999); });
        require(warned, "Unwritable effects settings did not report their path.");
    }
    app.setProperty("settingsFileForTests", path);
    log << "Effects: portable INI, all five restored values, 999 ms/100% limits, theme coexistence, persistent defaults, malformed values and write failure PASS\n"; log.flush();
}
void checkRendererSettings(QApplication &app, QTextStream &log, const QDir &output) {
    auto edit = [&](ScopeWindow &scope, const QString &expected, const QString &selected, bool save) {
        scope.show(); scope.findChild<QPushButton *>("editLayout")->setChecked(true); pump(app);
        std::exception_ptr failure; bool opened = false;
        QTimer::singleShot(30, &scope, [&] {
            auto dialog = scope.findChild<QDialog *>("scopeRendererDialog");
            if (!dialog) return;
            opened = true;
            try {
                auto choice = dialog->findChild<QComboBox *>("rendererChoice");
                require(choice && choice->currentData().toString() == expected, "Renderer selection not restored.");
                choice->setCurrentIndex(choice->findData(selected));
                require(choice->currentIndex() >= 0, "Renderer option absent.");
                if (save) {
                    dialog->grab().save(output.filePath("renderer-dialog.png"));
                    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
                } else dialog->reject();
            } catch (...) { failure = std::current_exception(); dialog->reject(); }
        });
        scope.findChild<QPushButton *>("scopeRenderer")->click();
        require(opened, "Renderer dialog unavailable.");
        if (failure) std::rethrow_exception(failure);
    };
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat); ini.setValue("Other/Keep", "untouched"); }
    {
        ScopeWindow first;
        auto gpu = first.findChild<QQuickWidget *>("scopeGpu");
        edit(first, "auto", "opengl", true);
        require(first.findChild<QQuickWidget *>("scopeGpu") == gpu, "Saved renderer changed the live GPU widget.");
        require(scopeRendererPreference() == "opengl", "Renderer choice was not saved.");
    }
    { ScopeWindow reopened; edit(reopened, "opengl", "cpu", false);
      require(scopeRendererPreference() == "opengl", "Cancel changed the saved renderer."); }
    const auto oldSoftware = qgetenv("BITMUSIC_SOFTWARE_SCOPES");
    qputenv("BITMUSIC_SOFTWARE_SCOPES", "1");
    {
        ScopeWindow software;
        require(!software.findChild<QQuickWidget *>("scopeGpu"), "CPU scope test created a GPU widget.");
        edit(software, "opengl", "auto", true);
        require(scopeRendererPreference() == "auto", "Cannot select GPU rendering from CPU mode.");
    }
    if (oldSoftware.isNull()) qunsetenv("BITMUSIC_SOFTWARE_SCOPES"); else qputenv("BITMUSIC_SOFTWARE_SCOPES", oldSoftware);
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat); ini.setValue("Renderer/Backend", "invalid"); ini.sync(); }
    require(scopeRendererPreference() == "auto", "Invalid renderer did not fall back to Auto.");
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
      require(ini.value("Other/Keep").toString() == "untouched", "Renderer save lost unrelated settings.");
      ini.remove("Renderer"); }
    log << "Renderer choice: save/restore, Cancel, deferred activation, CPU-to-GPU selection, invalid fallback and settings coexistence PASS\n";
}
}

int runGpuChecks(QApplication &app, const QStringList &arguments) {
    const int option = arguments.indexOf("--gpu-checks");
    QDir dir(option+1 < arguments.size() ? arguments[option+1] : "test-output"); dir.mkpath(".");
    QFile file(dir.filePath("gpu-checks.txt")); if (!file.open(QIODevice::WriteOnly)) return 2;
    QTextStream log(&file);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings; app.setProperty("settingsFileForTests", settings.filePath("settings.ini"));
    app.setProperty("themesDirectoryForTests", settings.filePath("themes"));
    try {
        checkRendererSettings(app, log, dir);
        checkEffectSettings(app, log);
        // Deliberately separated straight lines make stale trails and glow measurable.
        ScopeGpu canvas(nullptr); canvas.resize(600, 220); canvas.show();
        QString error; canvas.failed = [&](const QString &message) { error = message; };
        // Expose/resize may happen before the first chrome image is submitted.
        pump(app); canvas.resize(610, 230); pump(app); canvas.resize(600, 220);
        QImage chrome(QSize(600,220)*canvas.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        chrome.setDevicePixelRatio(canvas.devicePixelRatioF()); chrome.fill(Qt::black);
        ScopeLane a{0, QRectF(10,10,580,200), QColor("#60e6ce"), {{20,60}, {580,60}}};
        ScopeLane b = a; b.points = {{20,130},{580,130}};
        ScopeEffects off; off.glow = off.trail = false;
        canvas.submit(chrome, {a}, "one", 1, 0, true, off); pump(app);
        require(error.isEmpty(), "GPU initialization failed.");
        require(canvas.backend() != "Software" && canvas.backend() != "Initializing", "Test did not select a hardware graphics API.");
        log << "Backend: " << canvas.backend() << "; DPR=" << canvas.devicePixelRatioF() << '\n'; log.flush();
        auto capture = [&] { auto image = canvas.grabFramebuffer(); image.setDevicePixelRatio(canvas.devicePixelRatioF()); return image; };
        const auto plain = capture(); plain.save(dir.filePath("gpu-plain.png"));
        const QRectF core(50,60,500,.5), halo(50,62,500,2);
        require(brightness(plain, core) > 100000, "GPU trace is missing.");
        ScopeEffects glow = off; glow.glow = true;
        canvas.submit(chrome, {a}, "one", 1, 0, true, glow); pump(app);
        const auto lit = capture();
        require(brightness(lit, halo) > brightness(plain, halo)+1000, "Glow did not add a halo.");
        require(std::abs(brightness(lit,core)-brightness(plain,core)) < 3000, "Glow obscured the sharp current trace.");
        lit.save(dir.filePath("gpu-glow.png"));
        glow.glowStrength = 1;
        canvas.submit(chrome, {a}, "one", 1, 0, true, glow); pump(app);
        require(brightness(capture(),halo) > brightness(lit,halo)*2, "100% glow was capped at the previous limit.");
        ScopeEffects trail = off; trail.trail = true; trail.trailMs = 400; trail.trailStrength = 1;
        canvas.submit(chrome, {a}, "one", 1, 0, true, trail); pump(app, 70);
        canvas.submit(chrome, {b}, "one", 1, 70, true, trail); pump(app, 20);
        const auto early = capture(); require(canvas.historySize() == 2, "Previous trace was not retained.");
        require(brightness(early,core) > 10000, "Historical trace missing.");
        pump(app, 150); const auto late = capture();
        require(brightness(late,core) < brightness(early,core)/2, "Trail does not fade with elapsed time.");
        early.save(dir.filePath("gpu-trail.png"));
        canvas.submit(chrome, {b}, "one", 2, 0, true, trail); pump(app);
        require(canvas.historySize() == 1 && brightness(capture(),core) == 0, "Seek generation retained a stale trace.");
        canvas.submit(chrome, {a}, "one", 2, 70, true, trail); pump(app);
        canvas.submit(chrome, {a}, "one", 2, 70, false, trail); pump(app);
        const auto paused = capture(); pump(app, 150);
        require(paused == capture(), "Paused trail keeps changing.");
        canvas.submit(chrome, {b}, "changed-layout", 2, 70, true, trail); pump(app);
        require(canvas.historySize() == 1, "Layout/color/mask change retained a stale trace.");
        for (int i = 0; i < 200; ++i) canvas.submit(chrome, {a}, "changed-layout", 2, 100+i, true, trail);
        require(canvas.historySize() <= 64, "History exceeds its memory bound.");
        trail.trailMs = 999;
        canvas.submit(chrome, {a}, "long-trail", 3, 0, true, trail); pump(app, 70);
        for (int i = 1; i <= 18; ++i) {
            canvas.submit(chrome, {b}, "long-trail", 3, i*33, true, trail); pump(app, 33);
        }
        require(canvas.historySize() > 13 && brightness(capture(),core) > 0, "999 ms trail was truncated at the old frame limit.");
        pump(app, 400);
        require(brightness(capture(),core) == 0, "Long trail did not expire after its duration.");
        canvas.suspend(); canvas.hide(); pump(app);
        const auto rendered = canvas.renderedFrames(); pump(app, 100);
        require(canvas.historySize() == 0 && rendered == canvas.renderedFrames(), "Hidden canvas continues rendering.");
        log << "Actual framebuffer: core/glow, elapsed-time trail, seek/layout reset, pause, bounded history, hidden suspension PASS\n"; log.flush();

        TimedScopes scopes; scopes.resize(940,650); scopes.show();
        auto integratedGpu = dynamic_cast<ScopeGpu *>(scopes.findChild<QQuickWidget *>("scopeGpu"));
        require(integratedGpu, "Integrated GPU canvas absent: check BITMUSIC_SOFTWARE_SCOPES / QT_QUICK_BACKEND.");
        PlayerState state; state.valid = state.playing = true; state.generation = 1; state.info.path = "synthetic";
        state.info.title = "GPU scope check"; state.info.stereoOutput = true;
        ScopeFrame frame; frame.generation = 1;
        for (int count : {4, 8, 32}) {
            state.info.voices.clear(); frame.channels.clear(); frame.left.clear(); frame.right.clear();
            for (int ch = 0; ch < count; ++ch) {
                state.info.voices << QString("Voice %1").arg(ch+1);
                QVector<float> row(ScopeFrames), right(ScopeFrames);
                for (int i = 0; i < ScopeFrames; ++i) {
                    row[i] = float(9000*std::sin(i*.13) + 2400*std::sin(i*1.73));
                    right[i] = float(8500*std::sin(i*.11));
                }
                frame.channels << row; frame.left << row; frame.right << right;
            }
            frame.mask = count == 32 ? 0xffffffffu : (1u<<count)-1;
            scopes.present(state,frame); pump(app);
            if (count == 32) {
                // The automatic 16x2 grid has no plot height at this window size.
                scopes.findChild<QSpinBox *>("gridRows")->setValue(8);
                scopes.findChild<QSpinBox *>("gridColumns")->setValue(4);
                require(scopes.cardRect(0).height() > 44, "32-card benchmark has no drawable waveforms.");
            }
            if (!scopes.findChild<QPushButton *>("scopeEffects")->isEnabled()) {
                log << scopes.findChild<QPushButton *>("scopeEffects")->toolTip() << '\n'; log.flush();
                throw std::runtime_error("Integrated scopes fell back to software rendering.");
            }
            for (bool stereo : {false, true}) {
                scopes.findChild<QCheckBox *>("scopeStereo")->setChecked(stereo);
                scopes.paints.clear(); QVector<double> intervals; QElapsedTimer wall; wall.start(); qint64 previous = 0;
                QTimer timer; timer.setTimerType(Qt::PreciseTimer); timer.setInterval(33);
                QObject::connect(&timer, &QTimer::timeout, &scopes, [&] {
                    auto now = wall.nsecsElapsed(); if (previous) intervals << (now-previous)/1e6; previous = now;
                    frame.positionMs += 33;
                    // Moving phase produces distinct histories without changing autoscale.
                    for (int ch = 0; ch < count; ++ch) std::rotate(frame.channels[ch].begin(), frame.channels[ch].begin()+37, frame.channels[ch].end());
                    scopes.present(state,frame);
                });
                timer.start(); pump(app, 1300); timer.stop();
                auto report = [&](QVector<double> values) {
                    std::sort(values.begin(), values.end()); require(!values.isEmpty(), "No benchmark frames.");
                    return QString("median=%1 p95=%2 max=%3 ms").arg(values[values.size()/2],0,'f',3)
                        .arg(values[int(values.size()*.95)],0,'f',3).arg(values.back(),0,'f',3);
                };
                log << count << " cards " << (stereo ? "stereo" : "mono") << ": paint " << report(scopes.paints)
                    << "; GUI interval " << report(intervals) << '\n'; log.flush();
                if (count == 4) scopes.grab().save(dir.filePath(stereo ? "gpu-stereo.png" : "gpu-mono.png"));
                if (count == 4 && stereo) {
                    scopes.findChild<QPushButton *>("editLayout")->setChecked(true); pump(app);
                    bool checked = false;
                    QTimer::singleShot(60, &scopes, [&] {
                        auto dialog = qobject_cast<QDialog *>(app.activeModalWidget());
                        if (!dialog) return;
                        dialog->grab().save(dir.filePath("gpu-effects-dialog.png"));
                        auto trailToggle = dialog->findChild<QCheckBox *>("effectTrail");
                        auto glowToggle = dialog->findChild<QCheckBox *>("effectGlow");
                        auto duration = dialog->findChild<QSpinBox *>("effectDuration");
                        trailToggle->setChecked(false); glowToggle->setChecked(false);
                        checked = !duration->isEnabled();
                        auto buttons = dialog->findChild<QDialogButtonBox *>();
                        buttons->button(QDialogButtonBox::RestoreDefaults)->click();
                        checked = checked && trailToggle->isChecked() && glowToggle->isChecked() && duration->value() == 180;
                        buttons->button(QDialogButtonBox::Close)->click();
                    });
                    scopes.findChild<QPushButton *>("scopeEffects")->click();
                    require(checked, "Effects dialog did not apply controls/reset.");
                    scopes.findChild<QPushButton *>("editLayout")->setChecked(false);
                }
            }
        }
        auto gpu = dynamic_cast<ScopeGpu *>(scopes.findChild<QQuickWidget *>("scopeGpu"));
        scopes.showMinimized(); pump(app);
        require(gpu && !gpu->isVisible() && gpu->historySize() == 0, "Minimized scopes kept GPU history.");
        auto minimizedFrames = gpu->renderedFrames(); pump(app, 150);
        require(minimizedFrames == gpu->renderedFrames(), "Minimized scopes keep rendering.");
        scopes.showNormal(); scopes.present(state,frame); pump(app);
        require(gpu->isVisible() && gpu->historySize() == 1, "Restored scopes did not begin with fresh history.");
        scopes.hide(); pump(app);
        require(gpu && !gpu->isVisible() && gpu->historySize() == 0, "ScopeWindow did not suspend the GPU canvas.");
        const auto count = gpu->renderedFrames(); pump(app, 150);
        require(count == gpu->renderedFrames(), "ScopeWindow keeps rendering while hidden.");
        log << "Integrated 4/8/32-card mono/stereo, hidden-window suspension PASS\n";
        scopes.show(); scopes.present(state, frame); pump(app);
        QPointer<ScopeGpu> stalled = gpu;
        // Simulate loss of render-completion notifications while GUI/data stay live.
        gpu->quickWindow()->blockSignals(true);
        QElapsedTimer deadline; deadline.start();
        while (stalled && deadline.elapsed() < 7500) {
            frame.positionMs += 50; scopes.present(state, frame); pump(app, 50);
        }
        require(stalled.isNull(), "Stalled GPU widget was not released.");
        require(!scopes.findChild<QPushButton *>("scopeEffects")->isEnabled(), "GPU failure kept effects enabled.");
        state.generation++; frame.generation = state.generation; state.info.path = "after-gpu-failure";
        state.info.voices = {"New voice"}; frame.mask = 1;
        frame.channels.resize(1); frame.left.resize(1); frame.right.resize(1);
        scopes.beginFile(); scopes.resize(760, 520); scopes.present(state, frame); pump(app);
        const auto before = scopes.grab().toImage();
        frame.channels[0].fill(0); frame.left[0].fill(0); frame.right[0].fill(0);
        frame.positionMs += 50; scopes.present(state, frame); pump(app);
        require(before != scopes.grab().toImage(), "CPU fallback did not paint changing samples after resize/file change.");
        log << "Render-completion stall: GPU released, CPU scopes survive resize/file change and keep updating PASS\n";
        return 0;
    } catch (const std::exception &error) { log << "FAIL: " << error.what() << '\n'; return 1; }
}

// Explicit diagnostic run: real audio at zero volume, bounded history, repeated
// transport/window changes. Inputs stay read-only and preferences are isolated.
int runGpuSoak(QApplication &app, const QStringList &arguments) {
    const int option = arguments.indexOf("--gpu-soak");
    if (option + 3 >= arguments.size()) return 2;
    bool valid = false; const int seconds = arguments[option+2].toInt(&valid);
    if (!valid || seconds < 10 || seconds > 3600) return 2;
    QDir dir(arguments[option+1]); if (!dir.mkpath(".")) return 3;
    QFile report(dir.filePath("gpu-soak.txt")); if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream log(&report);
    app.setQuitOnLastWindowClosed(false);
    QTemporaryDir settings;
    if (!settings.isValid()) return 4;
    app.setProperty("settingsFileForTests", settings.filePath("settings.ini"));
    app.setProperty("themesDirectoryForTests", settings.filePath("themes"));
    { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
      ini.setValue("Effects/TrailDurationMs", 999); ini.setValue("Effects/GlowBrightness", 100); }
    try {
        auto paths = arguments.mid(option+3);
        int firstScreen = 0;
        for (const auto &argument : paths) if (argument.startsWith("--screen=")) {
            firstScreen = argument.mid(9).toInt(&valid);
            require(valid && firstScreen >= 0 && firstScreen < app.screens().size(), "Invalid screen index.");
        }
        paths.erase(std::remove_if(paths.begin(), paths.end(), [](const QString &path) { return path.startsWith("--screen="); }), paths.end());
        require(!paths.isEmpty(), "No music files supplied.");
        // Exercise startup from saved geometry on the selected monitor, before
        // constructing the GPU window, as well as live moves between monitors.
        QWidget placement; placement.setScreen(app.screens()[firstScreen]);
        const auto available = app.screens()[firstScreen]->availableGeometry();
        placement.setGeometry(QRect(available.topLeft()+QPoint(24,24), QSize(1000,700)));
        { QSettings ini(playerSettingsPath(), QSettings::IniFormat); ini.setValue("Windows/Scopes", placement.saveGeometry()); }
        PlayerWindow window; window.player().setVolume(0); window.show();
        for (const auto screen : app.screens())
            log << "screen=" << screen->name() << " geometry=" << screen->geometry().width() << 'x'
                << screen->geometry().height() << " dpr=" << screen->devicePixelRatio()
                << " Hz=" << screen->refreshRate() << '\n';
        window.loadFile(paths.front());
        auto &scopes = window.scopeWindow();
        QPointer<ScopeGpu> gpu = dynamic_cast<ScopeGpu *>(scopes.findChild<QQuickWidget *>("scopeGpu"));
        require(gpu, "GPU canvas absent.");
        QElapsedTimer wall; wall.start();
        quint64 rendered = 0; int framePosition = -1, cycle = -1, lastResize = -1, lastReport = -1;
        qint64 renderChanged = 0, scopeChanged = 0;
        while (wall.elapsed() < qint64(seconds)*1000) {
            const auto ms = wall.elapsed(); const int nextCycle = int(ms/15000);
            if (nextCycle != cycle) {
                cycle = nextCycle;
                const auto state = window.player().state();
                window.loadFile(paths[cycle % paths.size()], state.info.songs > 1 && paths.size() == 1 ? cycle % state.info.songs : 0);
                if (scopes.windowHandle() && app.screens().size() > 1) {
                    auto screen = app.screens()[(firstScreen+cycle) % app.screens().size()];
                    scopes.windowHandle()->setScreen(screen);
                    scopes.move(screen->availableGeometry().topLeft() + QPoint(24,24));
                }
                renderChanged = scopeChanged = ms; framePosition = -1;
            }
            if (ms % 15000 >= 7500 && ms/250 != lastResize) {
                lastResize = int(ms/250);
                scopes.resize(600+(lastResize*47)%1000, 400+(lastResize*29)%500);
            }
            pump(app, 50);
            require(gpu, "GPU fallback occurred during the soak; see diagnostic log.");
            const auto state = window.player().state(); const auto frame = window.player().scopes();
            require(state.error.isEmpty() && state.scopeError.isEmpty(), "Playback/scope error; see diagnostic state.");
            if (gpu->renderedFrames() != rendered) { rendered = gpu->renderedFrames(); renderChanged = wall.elapsed(); }
            if (frame.positionMs != framePosition) { framePosition = frame.positionMs; scopeChanged = wall.elapsed(); }
            if (lastReport != ms/1000) {
                lastReport = int(ms/1000);
                log << "t=" << ms << " backend=" << gpu->backend() << " gen=" << state.generation
                    << " audio=" << state.positionMs << " scopes=" << frame.positionMs << " mask=" << frame.mask
                    << " rendered=" << rendered << " history=" << gpu->historySize()
                    << " audioErrors=" << state.starvations << '/' << state.outputErrors
                    << " screen=" << scopes.screen()->name() << " completed=" << gpu->completedFrames() << '\n'; log.flush();
            }
            require(gpu->historySize() <= 64, "GPU history exceeds bound.");
            require(wall.elapsed()-renderChanged < 4000, "GPU rendering stopped.");
            require(state.busy || !state.playing || wall.elapsed()-scopeChanged < 6000, "Scope producer stopped.");
        }
        window.close(); log << "GPU playback/resize/track-change soak PASS\n"; return 0;
    } catch (const std::exception &error) { log << "FAIL: " << error.what() << '\n'; return 1; }
}
