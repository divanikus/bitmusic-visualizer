#include "window.h"
#include "outputhistory.h"
#include "appsettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pump(QApplication &app, int ms = 50) {
    QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
void until(QApplication &app, const std::function<bool()> &condition, const char *message) {
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < 5000) pump(app, 10);
    require(condition(), message);
}
QVector<float> tone(float amplitude, float phase = 0, float period = 137.3f) {
    QVector<float> row(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) row[i] = amplitude * std::sin((i + phase) * 6.283185307 / period);
    return row;
}
void checkEnvelopeAndTrigger() {
    WaveOptions options; options.trigger = WaveOptions::Off;
    const QVector<float> loud(ScopeFrames, 10000), quiet(ScopeFrames, 2000);
    ScopeWave wave; wave.update(loud, loud, 0, options);
    for (int ms = 20; ms <= 300; ms += 20) wave.update(quiet, quiet, ms, options);
    require(wave.peak == 10000, "Autoscale amplified a decay during Hold.");
    for (int ms = 320; ms <= 1300; ms += 20) wave.update(quiet, quiet, ms, options);
    require(std::abs(wave.peak - (2000 + 8000/std::exp(1.))) < 1, "Release is not based on elapsed audio time.");
    ScopeWave coarse; coarse.update(loud, loud, 0, options);
    for (int ms = 50; ms <= 1300; ms += 50) coarse.update(quiet, quiet, ms, options);
    require(std::abs(coarse.peak - wave.peak) < 1, "Autoscale changes speed with frame rate.");
    const float held = wave.peak;
    for (int i = 0; i < 20; ++i) wave.update(quiet, quiet, 1300, options);
    require(wave.peak == held, "Repainting a paused frame changes amplitude.");
    wave.update(loud, loud, 1320, options); require(wave.peak == 10000, "Louder note does not attack immediately.");
    wave.update(quiet, quiet, 0, options); require(wave.peak == 2000, "Rewind retained old normalization.");
    options.scale = WaveOptions::Fixed; wave.update(loud, quiet, 20, options); require(wave.peak == 32768, "Fixed scale is not full-scale PCM.");
    options.scale = WaveOptions::Instant; wave.update(quiet, quiet, 40, options); require(wave.peak == 2000, "Instant autoscale retained a peak.");
    options.trigger = WaveOptions::Stable;
    auto noisy = tone(7000, -1100, 256);
    noisy[1049] = -2; noisy[1050] = 2; // A tiny false rising edge before the real lobe.
    ScopeWave noiseTrigger; noiseTrigger.update(noisy, noisy, 0, options);
    require(std::abs(noiseTrigger.start - 1100) < 1, "Stable trigger accepted a tiny false crossing.");
    WaveOptions rising = options; rising.trigger = WaveOptions::Rising;
    ScopeWave rawTrigger; rawTrigger.update(noisy, noisy, 0, rising);
    require(rawTrigger.start < 1051, "Rising edge mode no longer offers the simple trigger.");
    ScopeWave trigger;
    for (int tick = 0; tick < 120; ++tick) {
        auto row = tone(7000, tick * 43.7f); const auto copy = row;
        trigger.update(row, row, tick*25, options);
        require(trigger.start >= 1023 && trigger.start < 2048 && std::abs(ScopeWave::sample(row, trigger.start)) < .1,
                "Stable trigger lost sub-sample rising-edge alignment.");
        require(row == copy, "Presentation modified source PCM.");
    }
    ScopeWave pan;
    auto left = tone(7000), right = tone(7700, 50);
    pan.update(left, left, 0, options); pan.update(left, right, 25, options);
    require(pan.triggerSide == 0, "Small stereo level change swapped the trigger source.");
    right = tone(21000, 50); pan.update(left, right, 50, options);
    require(pan.triggerSide == 1, "Dominant stereo side could not acquire the trigger.");
    options.trigger = WaveOptions::Off; pan.update(left, right, 75, options);
    require(pan.start == 1024, "Trigger Off still shifted the waveform.");
}
void checkCapture() {
    OutputHistory history;
    const std::array<short, 8> pcm{30000,-24000,1000,-500,32,-16,0,0};
    auto scaled = pcm; applyOutputGain(scaled.data(), scaled.size(), .5f, 1);
    history.push(scaled.data(), 4, 1, 3, 10);
    auto first = history.read();
    require(first.sequence && first.generation == 1 && first.token == 3 && first.left[ScopeFrames-4] == 15000 &&
        first.right[ScopeFrames-4] == 0 && first.left[0] == 0, "Output capture lost post-gain/LR PCM or startup padding.");
    std::atomic<bool> done{false}, observedWrap{false}; bool bad = false; int reads = 0;
    std::thread writer([&] {
        std::array<short, 326> block{};
        for (int count = 0; count < 600000; count += 163) {
            for (int i = 0; i < 163; ++i) { block[i*2] = short((count+i)%30001); block[i*2+1] = -block[i*2]; }
            history.push(block.data(), 163, 2, 5, count+163);
            // One explicit interleaving after a wrap makes this deterministic:
            // a bounded reader need not succeed against an uninterrupted writer.
            if (count/163 == 64) while (!observedWrap) std::this_thread::yield();
        }
        done = true;
    });
    do {
        const auto frame = history.read();
        if (frame.generation == 2 && frame.sequence && frame.positionMs >= ScopeFrames) {
            ++reads;
            for (int i = 0; i < ScopeFrames; ++i) {
                const int value = (frame.positionMs-ScopeFrames+i)%30001;
                if (frame.left[i] != value || frame.right[i] != -value) bad = true;
            }
            if (frame.positionMs == 65*163) observedWrap = true;
        }
    } while (!done);
    writer.join();
    const auto last = history.read();
    if (bad || !reads || !last.sequence || last.left.back() != (last.positionMs-1)%30001)
        throw std::runtime_error(QString("Concurrent output snapshot: torn=%1, successful reads=%2, final sequence=%3, last=%4, expected=%5")
            .arg(bad).arg(reads).arg(last.sequence).arg(last.left.isEmpty() ? -1 : last.left.back()).arg((last.positionMs-1)%30001).toStdString());
    history.push(pcm.data(), 4, 3, 7, 0);
    auto reset = history.read();
    require(reset.generation == 3 && reset.token == 7 && reset.left[ScopeFrames-5] == 0 && reset.right.back() == 0,
            "Output history retained audio across generation/capture reset.");
}
void edit(QApplication &app, ScopeWindow &scope, const std::function<void(QDialog *)> &action) {
    scope.show(); scope.findChild<QPushButton *>("editLayout")->setChecked(true); pump(app);
    bool opened = false; std::exception_ptr error;
    QTimer::singleShot(30, &scope, [&] {
        auto dialog = scope.findChild<QDialog *>("scopeWavesDialog"); if (!dialog) return;
        opened = true;
        try { action(dialog); } catch (...) { error = std::current_exception(); }
        dialog->reject();
    });
    scope.findChild<QPushButton *>("scopeWaves")->click();
    require(opened, "Waveforms dialog did not open."); if (error) std::rethrow_exception(error);
}
QListWidgetItem *mixItem(ScopeWindow &scope) {
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toInt() == ScopeTheme::FullMix) return list->item(i);
    throw std::runtime_error("Full mix is missing from the channel visibility list.");
}
void checkWaveUi(QApplication &app, const QString &directory) {
    const QDir dir(directory);
    {
        ScopeTheme theme; theme.fullMix.waveform = QColor("#EF1234");
        QString error; const auto path = dir.filePath("full-mix-theme.ini");
        require(theme.save(path, error), "Cannot save extended Full mix theme.");
        ScopeTheme loaded; require(loaded.load(path, error) && loaded.fullMix.waveform == theme.fullMix.waveform && loaded.tiles[0].waveform != theme.fullMix.waveform,
            "Full mix palette did not roundtrip independently.");
        { QSettings ini(path, QSettings::IniFormat); ini.remove("FullMix"); }
        require(loaded.load(path, error) && loaded.fullMix.waveform == loaded.tiles[0].waveform, "Old 32-slot theme did not load with compatible mix colors.");
        { QSettings ini(path, QSettings::IniFormat); ini.setValue("FullMix/Waveform", "bad"); }
        require(!loaded.load(path, error) && loaded.fullMix.waveform == loaded.tiles[0].waveform, "Malformed mix colors were accepted or changed the current theme.");
    }
    { QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setValue("Other/WaveKeep", 42); }
    {
        ScopeWindow scope; scope.resize(920, 700);
        PlayerState state; state.generation = 1; state.valid = true; state.info.voices = {"Pulse 1"}; state.info.title = "Waveform preview";
        ScopeFrame frame; frame.generation = 1; frame.mask = 1; frame.channels = {tone(7000)};
        OutputFrame output; output.generation = 1; output.sequence = 2; output.left = tone(7000); output.right = QVector<float>(ScopeFrames, 0);
        scope.present(state, frame);
        uint32_t voiceMask = 0; bool capturing = false;
        scope.onChannelsChanged = [&](uint32_t mask) { voiceMask = mask; };
        scope.onOutputChanged = [&](bool enabled) { capturing = enabled; };
        require(mixItem(scope)->checkState() == Qt::Unchecked && !scope.outputEnabled(), "Full mix must start unchecked.");
        edit(app, scope, [&](QDialog *dialog) {
            dialog->findChild<QComboBox *>("waveScale")->setCurrentIndex(WaveOptions::Fixed);
            require(!dialog->findChild<QSpinBox *>("waveHold")->isEnabled(), "Hold stays active for Fixed scale.");
            dialog->findChild<QComboBox *>("waveScale")->setCurrentIndex(WaveOptions::Smooth);
            dialog->findChild<QComboBox *>("waveTrigger")->setCurrentIndex(WaveOptions::Off);
            dialog->findChild<QSpinBox *>("waveHold")->setValue(777);
            dialog->findChild<QSpinBox *>("waveRelease")->setValue(2345);
            require(!dialog->findChild<QCheckBox *>("waveOutput") && !dialog->findChild<QLabel *>("waveScaleHelp")->text().isEmpty() &&
                dialog->findChild<QLabel *>("waveTriggerHelp")->text().contains("Does not align"), "Waveform explanations or list-only mix control missing.");
            dialog->grab().save(dir.filePath("native-waveforms-dialog.png"));
        });
        mixItem(scope)->setCheckState(Qt::Checked);
        require(!capturing && !scope.outputEnabled() && mixItem(scope)->toolTip().contains("Overflow"), "Overflow mix still captures audio.");
        scope.findChild<QSpinBox *>("gridColumns")->setValue(2);
        require(capturing && voiceMask == 1 && scope.visibleChannels() == QVector<int>({0, ScopeTheme::FullMix}), "Mix entered the voice mask or failed to occupy one cell.");
        scope.findChild<QPushButton *>("applyLayout")->click(); scope.present(state, frame, output); pump(app);
        require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(1) && scope.panelRect(0).size() == scope.panelRect(ScopeTheme::FullMix).size(), "Mix is not a normal one-cell card.");
        auto capture = [&] {
            if (auto gpu = scope.findChild<QQuickWidget *>("scopeGpu")) return gpu->grabFramebuffer();
            return scope.grab().toImage();
        };
        auto first = capture(); first.save(dir.filePath("native-output-mix.png"));
        output.sequence += 2; output.right = tone(5500, 50); scope.present(state, frame, output); pump(app);
        require(first != capture(), "Output updates were lost when the chip-scope clock did not change.");
        // Both snapshots start with a fresh scale and a completed paint. A
        // queued GPU repaint must not be mistaken for a stereo-lane change.
        scope.findChild<QCheckBox *>("scopeStereo")->setChecked(true); pump(app);
        const auto monoPicture = capture();
        scope.findChild<QCheckBox *>("scopeStereo")->setChecked(false); scope.present(state, frame, output); pump(app);
        require(monoPicture == capture(), "Mono mix split into stereo lanes.");
        scope.findChild<QCheckBox *>("scopeStereo")->setChecked(true);
        state.info.stereoOutput = true; scope.present(state, frame, output); pump(app);
        require(monoPicture != capture(), "Stereo preference did not split the mix inside its card.");
        scope.resize(480, 360); pump(app); require(scope.panelRect(ScopeTheme::FullMix).size() == scope.panelRect(0).size(), "Small mix card has a special size.");
        scope.grab().save(dir.filePath("native-output-small.png"));
        scope.findChild<QPushButton *>("editLayout")->setChecked(true);
        scope.resize(920, 700); pump(app);
        auto list = scope.findChild<QListWidget *>("scopeChannelList");
        require(list->model()->moveRows({}, 1, 1, {}, 0), "Mix list reorder failed.");
        require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(0) && voiceMask == 1, "Mix reorder altered voice identities.");
        auto mouse = [&](QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, scope.mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier); QApplication::sendEvent(&scope, &event);
        };
        const auto from = scope.cardRect(0).topLeft()+QPointF(15,15), to = scope.cardRect(1).topLeft()+QPointF(15,15);
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton); mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton); pump(app, 220);
        require(scope.panelRect(ScopeTheme::FullMix) == scope.cardRect(1), "Full mix grid drag did not settle into the target cell.");
        bool colorsOpened = false;
        QTimer::singleShot(30, &scope, [&] {
            auto dialog = scope.findChild<QDialog *>("scopeColorsDialog"); if (!dialog) return;
            colorsOpened = dialog->findChild<QComboBox *>("colorChannel")->currentIndex() == ScopeTheme::FullMix;
            dialog->findChild<QLineEdit *>("backgroundHex")->setText("#394755");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        const auto pipette = scope.panelRect(ScopeTheme::FullMix).topRight()+QPointF(-44,14);
        mouse(QEvent::MouseButtonPress, pipette, Qt::LeftButton, Qt::LeftButton); mouse(QEvent::MouseButtonRelease, pipette, Qt::LeftButton, Qt::NoButton);
        require(colorsOpened, "Full mix pipette did not select its own palette.");
        scope.findChild<QPushButton *>("applyLayout")->click(); pump(app); capture().save(dir.filePath("native-full-mix-card.png"));
        scope.beginFile(); ++state.generation; scope.present(state, {});
        require(scope.outputEnabled(), "File opening reset waveform preferences.");
        state.info.voices.clear(); for (int i = 0; i < 32; ++i) state.info.voices << QString::number(i);
        scope.beginFile(); ++state.generation; scope.present(state, {});
        require(voiceMask == 0xffffffffu && scope.visibleChannels().size() == 33 && scope.outputEnabled(), "Full mix aliases a hardware bit at 32 voices.");
    }
    {
        ScopeWindow restored;
        edit(app, restored, [&](QDialog *dialog) {
            require(dialog->findChild<QComboBox *>("waveScale")->currentIndex() == WaveOptions::Smooth &&
                dialog->findChild<QComboBox *>("waveTrigger")->currentIndex() == WaveOptions::Off &&
                dialog->findChild<QSpinBox *>("waveHold")->value() == 777 &&
                dialog->findChild<QSpinBox *>("waveRelease")->value() == 2345, "Waveform preferences did not survive restart.");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
        });
        QSettings settings(playerSettingsPath(), playerSettingsFormat());
        require(settings.value("Waveforms/OutputMix").toBool(), "Waveform defaults reset the independent mix visibility.");
        restored.findChild<QPushButton *>("resetLayout")->click();
    }
    {
        QSettings settings(playerSettingsPath(), playerSettingsFormat());
        require(settings.value("Other/WaveKeep").toInt() == 42 && !settings.value("Waveforms/OutputMix").toBool(), "Reset lost unrelated settings or did not persist.");
        settings.setValue("Waveforms/Scale", "bogus"); settings.setValue("Waveforms/HoldMs", -5);
        settings.setValue("Waveforms/ReleaseMs", "bad"); settings.setValue("Waveforms/OutputMix", "bad");
    }
    {
        ScopeWindow invalid;
        edit(app, invalid, [&](QDialog *dialog) {
            require(dialog->findChild<QComboBox *>("waveScale")->currentIndex() == WaveOptions::Smooth &&
                dialog->findChild<QSpinBox *>("waveHold")->value() == 0 && dialog->findChild<QSpinBox *>("waveRelease")->value() == 1000 &&
                !invalid.outputEnabled(), "Invalid waveform preferences did not recover safely.");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
        });
    }
}
void checkLiveOutput(QApplication &app, const QString &path) {
    NativePlayer player; player.setVolume(.025f); player.setOutputScopesEnabled(true);
    player.load(path);
    until(app, [&] { return player.outputScopes().positionMs >= 250; }, "Live callback output was not captured.");
    const auto captured = player.outputScopes();
    // Match actual sink PCM to the normal full mix (not the dry voice taps).
    GmeTrack reference(path); const int end = (captured.positionMs * SampleRate / 1000) + 45;
    QVector<short> pcm(end*2); reference.render(pcm.data(), end); applyOutputGain(pcm.data(), size_t(pcm.size()), .025f, 3);
    bool match = false;
    for (int tail = end - 45; tail <= end && !match; ++tail) {
        bool equal = true;
        for (int i = 0; i < 256 && equal; ++i)
            equal = captured.left[ScopeFrames-256+i] == pcm[(tail-256+i)*2] && captured.right[ScopeFrames-256+i] == pcm[(tail-256+i)*2+1];
        match = equal;
    }
    require(match, "Output scope does not match the post-volume full audio mix.");
    player.setOutputMask(1); pump(app, 150);
    auto gated = player.outputScopes();
    require(std::all_of(gated.right.cbegin(), gated.right.cend(), [](float v) { return v == 0; }) &&
        std::any_of(gated.left.cbegin(), gated.left.cend(), [](float v) { return v != 0; }), "Live L/R gate did not reach the output scope.");
    player.setVolume(0); pump(app, 150);
    auto silent = player.outputScopes(); require(std::all_of(silent.left.cbegin(), silent.left.cend(), [](float v) { return v == 0; }), "Zero volume was not captured.");
    player.seek(player.state().positionMs, false); until(app, [&] { return !player.state().busy; }, "Pause output failed.");
    const auto paused = player.outputScopes(); pump(app, 80);
    require(paused.sequence && paused.sequence == player.outputScopes().sequence && paused.generation == player.state().generation, "Pause did not freeze delivered output.");
    player.seek(1700, false); until(app, [&] { return !player.state().busy; }, "Paused output seek failed.");
    require(!player.outputScopes().sequence, "Paused seek displayed audio from the previous position.");
    player.seek(500, true); until(app, [&] { auto f = player.outputScopes(); return f.sequence && f.generation == player.state().generation; }, "Resumed output did not recover.");
    player.setOutputScopesEnabled(false); require(!player.outputScopes().sequence, "Hidden output still exposes capture."); pump(app, 100);
    player.setOutputScopesEnabled(true); require(!player.outputScopes().sequence, "Reopened output displayed old history.");
    until(app, [&] { return player.outputScopes().sequence != 0; }, "Reopened output did not resume capture.");
    require(!player.state().starvations && !player.state().outputErrors, "Output capture starved audio.");
    player.shutdown();
}
void checkOutputVisibility(QApplication &app, const QString &directory) {
    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(QDir(directory).filePath("demo.spc"));
    until(app, [&] { return !player.player().state().busy && player.player().state().valid; }, "Output visibility file failed to load.");
    auto &scope = player.scopeWindow();
    player.refresh(); scope.findChild<QPushButton *>("editLayout")->setChecked(true);
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    mixItem(scope)->setCheckState(Qt::Checked);
    require(!player.player().outputScopes().sequence, "Overflow mix still captures output.");
    require(list->model()->moveRows({}, list->count()-1, 1, {}, 0), "Cannot move mix into the grid.");
    until(app, [&] { return player.player().outputScopes().sequence != 0; }, "Output checkbox did not enable capture.");
    for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toInt() != ScopeTheme::FullMix) list->item(i)->setCheckState(Qt::Unchecked);
    const auto before = player.player().outputScopes(); pump(app, 100);
    require(player.player().state().scopesSuspended && player.player().outputScopes().sequence > before.sequence, "Empty chip grid froze the output view.");
    scope.hide(); require(!player.player().outputScopes().sequence, "Hiding the window left output capture enabled.");
    scope.show(); until(app, [&] { return player.player().outputScopes().sequence != 0; }, "Showing the window did not resume output capture.");
    scope.showMinimized(); pump(app); require(!player.player().outputScopes().sequence, "Minimized window left output capture enabled.");
    scope.showNormal(); until(app, [&] { return player.player().outputScopes().sequence != 0; }, "Restoring the window did not resume output capture.");
    mixItem(scope)->setCheckState(Qt::Unchecked);
    require(!player.player().outputScopes().sequence, "Unchecking Output did not suspend capture.");
    player.close();
}
}
void checkWaves(QApplication &app, const QString &directory, QTextStream &log, const QStringList &musicPaths) {
    checkEnvelopeAndTrigger(); log << "Waves: hold/release cadence, attack, pause, rewind, scale modes, sub-sample trigger and stereo hysteresis PASS\n"; log.flush();
    checkCapture(); log << "Output history: concurrent 600000-frame wraps, packed L/R, generation reset and startup padding PASS\n"; log.flush();
    checkWaveUi(app, directory); log << "Waveform UI: explanations, persistence/defaults, normal Full mix card, overflow/capture, list/grid drag, pipette, mono/stereo, 32 voices + mix PASS\n"; log.flush();
    checkLiveOutput(app, QDir(directory).filePath("demo.spc"));
    for (const auto &path : musicPaths) checkLiveOutput(app, path);
    log << "Live output: " << 1 + musicPaths.size() << " files; full-mix PCM equality after volume, L/R gate, pause, seek, capture suspension; audio errors=0 PASS\n"; log.flush();
    checkOutputVisibility(app, directory); log << "Output UI wiring: enabled/disabled, empty chip grid, hidden/minimized/restored window PASS\n"; log.flush();
}
