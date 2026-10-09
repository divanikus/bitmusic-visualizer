#include "window.h"
#include "appsettings.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QAbstractItemView>
#include <QIcon>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QTimer>
#include <QTextStream>
#include <QSettings>
#include <QScreen>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>

void checkGbs(QApplication &app, const QString &directory, QTextStream &log);
void checkAy(QApplication &app, const QString &directory, QTextStream &log);
void checkWaves(QApplication &app, const QString &directory, QTextStream &log, const QStringList &musicPaths = {});
void checkViews(QApplication &app, const QString &directory, QTextStream &log);
void checkFrameRates(QApplication &app, const QString &directory, QTextStream &log);
void checkLayoutSpans(QApplication &app, const QString &directory, QTextStream &log);

namespace {
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
int peak(const std::array<short, BlockFrames * 2> &data) {
    int value = 0;
    for (short sample : data) value = std::max(value, std::abs(int(sample)));
    return value;
}
void pump(QApplication &app, int milliseconds) {
    // Drive timers and painting through an event loop, as during normal playback.
    QEventLoop loop(&app);
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
void until(QApplication &app, const std::function<bool()> &condition, const char *message, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout) { app.processEvents(); QThread::msleep(4); }
    require(condition(), message);
}
void checkRing() {
    PcmRing ring;
    constexpr int total = 500000;
    std::atomic<bool> bad{false};
    std::thread producer([&] {
        std::array<short, 777> block{};
        for (int written = 0; written < total;) {
            int count = std::min(int(block.size()), total - written);
            for (int i = 0; i < count; ++i) block[i] = short((written + i) % 30001);
            if (ring.push(block.data(), count)) written += count; else std::this_thread::yield();
        }
    });
    std::array<short, 513> block{};
    for (int read = 0; read < total;) {
        const auto count = ring.pop(block.data(), std::min(size_t(total - read), block.size()));
        for (size_t i = 0; i < count; ++i) if (block[i] != short((read + int(i)) % 30001)) bad = true;
        read += int(count);
        if (!count) std::this_thread::yield();
    }
    producer.join();
    require(!bad && ring.size() == 0, "Ring lost or reordered samples across callback sizes/wraps.");
}

void checkStereoOutputs(QApplication &app, const QString &directory) {
    // Exercise the actual callback output stage: no fold-down, crosstalk or extra gain.
    const std::array<short, 8> input{32000, -24000, -30000, 20000, 2, -2, 0, 400};
    const std::array<std::array<short, 8>, 4> expected{{
        {0, 0, 0, 0, 0, 0, 0, 0}, {16000, 0, -15000, 0, 1, 0, 0, 0},
        {0, -12000, 0, 10000, 0, -1, 0, 200}, {16000, -12000, -15000, 10000, 1, -1, 0, 200}}};
    for (uint32_t mask = 0; mask < 4; ++mask) {
        auto samples = input; applyOutputGain(samples.data(), samples.size(), .5f, mask);
        require(samples == expected[mask], "Output L/R mute altered the wrong samples.");
    }
    const QDir dir(directory);
    const auto mono = GmeTrack(dir.filePath("demo.nsf")).info();
    const auto stereo = GmeTrack(dir.filePath("demo.spc")).info();
    require(!mono.stereoOutput && !GmeTrack(dir.filePath("named.nsfe")).info().stereoOutput, "NES incorrectly marked stereo.");
    require(stereo.stereoOutput && GmeTrack(dir.filePath("demo.vgm")).info().stereoOutput &&
        GmeTrack(dir.filePath("demo.vgz")).info().stereoOutput, "Stereo-capable source incorrectly marked mono.");

    QTemporaryDir fixtures(dir.filePath("stereo-fixtures-XXXXXX")); require(fixtures.isValid(), "Cannot create stereo fixtures.");
    auto psgFixture = [&](bool stereoRouting, bool opll) {
        QByteArray data(0x100, '\0'); data.replace(0, 4, "Vgm ");
        qToLittleEndian<quint32>(0x150, data.data() + 8);
        qToLittleEndian<quint32>(3579545, data.data() + 12);
        if (opll) qToLittleEndian<quint32>(3579545, data.data() + 16);
        qToLittleEndian<quint32>(0xcc, data.data() + 0x34);
        // Stereo-like bytes inside data blocks, command operands and trailing metadata must be ignored.
        data += QByteArray::fromHex("676600020000004ff0504f4fff50805010509062");
        if (stereoRouting) data += QByteArray::fromHex("4ff0");
        data += QByteArray::fromHex("62664ff0");
        qToLittleEndian<quint32>(data.size() - 4, data.data() + 4);
        const auto path = fixtures.filePath(opll ? "opll.vgm" : stereoRouting ? "game-gear.vgm" : "sms.vgm");
        QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Cannot write stereo fixture."); file.close();
        return GmeTrack(path).info();
    };
    require(!psgFixture(false, false).stereoOutput, "Mono PSG mistaken for Game Gear routing.");
    require(psgFixture(true, false).stereoOutput, "PSG stereo routing not detected.");
    require(!psgFixture(true, true).stereoOutput, "YM2413 backend incorrectly marked stereo.");

    ScopeWindow scopes;
    PlayerState state; state.valid = true; state.generation = 1; state.info = mono;
    state.info.voices = {"Pulse 1"}; state.info.title = "Mono source";
    ScopeFrame frame; frame.generation = 1; frame.mask = 1;
    QVector<float> samples(ScopeFrames);
    for (int i = 0; i < ScopeFrames; ++i) samples[i] = float((i % 128) - 64) * 80;
    frame.channels = {samples}; frame.left = {samples}; frame.right = {samples};
    scopes.resize(740, 420); scopes.show(); scopes.present(state, frame); pump(app, 20);
    auto preference = scopes.findChild<QCheckBox *>("scopeStereo");
    auto picture = [&] { return scopes.grab(scopes.panelRect(0).toRect()).toImage(); };
    const auto monoPicture = picture(); preference->setChecked(true);
    require(!preference->isEnabled() && monoPicture == picture(), "Stereo preference split a mono card.");
    scopes.grab().save(dir.filePath("native-mono-scopes.png"));
    state.info = stereo; state.info.voices = {"DSP 1"}; state.info.title = "Stereo output preview";
    scopes.present(state, frame); pump(app, 20);
    require(preference->isEnabled() && preference->isChecked(), "Stereo preference lost on file change.");
    const auto plot = scopes.panelRect(0).adjusted(12, 32, -12, -10).toRect();
    auto lanes = [&] { return scopes.grab(plot).toImage(); };
    const auto both = lanes();
    const QRect upper(0, 0, both.width(), both.height() / 2), lower(0, both.height() / 2, both.width(), both.height() / 2);
    state.outputMask = 2; scopes.present(state, frame); const auto leftMuted = lanes();
    require(leftMuted.copy(upper) != both.copy(upper) && leftMuted.copy(lower) == both.copy(lower), "Left mute dimmed the wrong lane.");
    state.outputMask = 1; scopes.present(state, frame); const auto rightMuted = lanes();
    require(rightMuted.copy(upper) == both.copy(upper) && rightMuted.copy(lower) != both.copy(lower), "Right mute dimmed the wrong lane.");
    scopes.grab().save(dir.filePath("native-right-output-muted.png"));
    state.outputMask = 3; state.muteMask = 1; scopes.present(state, frame);
    require(lanes().copy(upper) == leftMuted.copy(upper) && lanes().copy(lower) == rightMuted.copy(lower), "Voice mute did not dim both lanes.");
    state.info = mono; state.info.voices = {"Pulse 1"}; state.muteMask = 0; scopes.present(state, frame);
    const auto restoredMono = lanes(); state.outputMask = 1; scopes.present(state, frame);
    require(restoredMono == lanes(), "One enabled mono output should retain the trace brightness.");
    state.outputMask = 0; scopes.present(state, frame); require(restoredMono != lanes(), "Both muted outputs left mono trace bright.");
    scopes.close();

    PlayerWindow player; player.player().setVolume(0); player.show(); player.loadFile(dir.filePath("demo.spc"));
    auto ready = [&] { auto s = player.player().state(); auto f = player.player().scopes(); return s.valid && !s.busy && f.generation == s.generation && f.mask; };
    until(app, ready, "Stereo UI did not load.");
    player.player().seek(1000, false); until(app, ready, "Stereo UI paused seek failed."); player.refresh();
    auto left = player.findChild<QToolButton *>("leftOutput"), right = player.findChild<QToolButton *>("rightOutput");
    require(left && right && left->isChecked() && right->isChecked(), "Output switches must default to on.");
    const auto before = player.player().state(); const auto beforeFrame = player.player().scopes();
    right->click(); require(player.player().state().outputMask == 1, "Right switch did not mute right output.");
    left->click(); require(player.player().state().outputMask == 0, "Output switches cannot both be muted.");
    right->click(); require(player.player().state().outputMask == 2, "Output switches are not independent.");
    require(player.player().state().generation == before.generation && player.player().scopes().left == beforeFrame.left &&
        player.player().scopes().right == beforeFrame.right, "Output switching restarted emulation or discarded scope signals.");
    player.player().seek(1300, true); until(app, [&] { return ready() && player.player().state().positionMs > 1350; }, "Playback stalled after output mute.");
    require(player.player().state().outputMask == 2, "Seek/resume lost output selection.");
    player.loadFile(dir.filePath("demo.nsf")); until(app, ready, "Mono file switch failed.");
    require(player.player().state().outputMask == 2, "File switch lost output selection.");
    player.player().seek(0, false); until(app, [&] { return !player.player().state().busy; }, "Stop failed after output mute.");
    player.refresh(); require(player.player().state().outputMask == 2, "Stop lost output selection.");
    player.grab().save(dir.filePath("native-output-controls.png"));
    player.resize(380, 330); pump(app, 20); player.grab().save(dir.filePath("native-output-controls-small.png"));
    require(left->geometry().right() < right->geometry().left() && player.rect().contains(right->geometry()), "Output controls overlap at minimum size.");
    require(player.player().state().starvations == before.starvations && player.player().state().outputErrors == before.outputErrors, "Output switching interrupted audio.");
    player.close();
}

void checkScopeSeeks(QApplication &app, const QStringList &paths, QTextStream &log, int fixtureCount = 0) {
    for (int index = 0; index < paths.size(); ++index) {
        const auto &path = paths[index];
        NativePlayer player;
        player.setVolume(0);
        player.setScopesEnabled(true);
        player.load(path);
        until(app, [&] { return !player.state().busy && player.state().valid; }, "Seek probe failed to load.");
        const auto initial = player.state();
        const uint32_t all = initial.info.voices.size() == 32 ? ~0u : (1u << initial.info.voices.size()) - 1;
        auto ready = [&] {
            const auto s = player.state(); const auto f = player.scopes();
            return !s.busy && f.generation == s.generation && f.mask == all && s.positionMs - f.positionMs < 200;
        };
        until(app, ready, "Initial scopes did not prepare.");
        const int late = initial.info.durationMs > 0 ? std::min(60000, initial.info.durationMs / 2) : 60000;
        for (const int position : {late, 250}) {
            QElapsedTimer elapsed; elapsed.start();
            player.seek(position, true);
            qint64 firstReady = -1;
            while (!ready() && elapsed.elapsed() < 15000) {
                pump(app, 10);
                const auto s = player.state(); const auto f = player.scopes();
                if (firstReady < 0 && !s.busy && f.generation == s.generation && f.mask) firstReady = elapsed.elapsed();
            }
            const auto s = player.state(); const auto f = player.scopes();
            log << "Seek " << QFileInfo(path).suffix() << " to " << position << " ms: " << elapsed.elapsed()
                << " ms; first=" << firstReady << " ms; ready=" << f.mask << '/' << all << "; lag=" << s.positionMs - f.positionMs
                << " ms; empty callbacks=" << s.starvations - initial.starvations << '\n'; log.flush();
            require(ready(), "Full scope grid did not catch up after seek within 15 seconds.");
            require(s.starvations == initial.starvations && s.outputErrors == initial.outputErrors, "Scope seek interrupted audio.");
        }
        player.seek(2000, false);
        until(app, ready, "Paused scope seek did not complete.");
        require(!player.state().playing && player.scopes().positionMs == 2000, "Paused scopes have the wrong timestamp.");
        if (index < fixtureCount) {
            const auto f = player.scopes();
            require(std::any_of(f.channels[0].begin(), f.channels[0].end(), [](float v) { return std::abs(v) > 100; }), "Seek lost active scope signal.");
            require(std::all_of(f.channels[1].begin(), f.channels[1].end(), [](float v) { return std::abs(v) <= 1; }), "Seek duplicated another voice into an unused scope.");
        }
        if (QFileInfo(path).suffix().compare("spc", Qt::CaseInsensitive) == 0) {
            for (const int boundary : {22000, 44000}) {
                if (initial.info.durationMs > 0 && initial.info.durationMs < boundary) continue;
                player.seek(boundary, false);
                until(app, ready, "SPC scopes stuck at a resampler buffer boundary.");
                require(!player.state().playing && player.scopes().positionMs == boundary,
                    "SPC boundary preparation changed the paused transport position.");
            }
            log << "SPC paused buffer-boundary seeks: all eight channels ready PASS\n"; log.flush();
        }
        // Cancel real preparation work with a newer seek and a hidden window.
        player.seek(late, true); pump(app, 30);
        player.seek(250, false); player.setScopesEnabled(false);
        until(app, [&] { return !player.state().busy && player.state().scopesSuspended; }, "Seek cancellation/suspension stuck.");
        player.setScopesEnabled(true);
        until(app, ready, "Cancelled scope work blocked the newest seek.");
        require(!player.state().playing && player.scopes().positionMs == 250, "Cancelled seek published stale scopes.");
        log << "Paused signals, cancellation and scope resume " << QFileInfo(path).suffix() << " PASS\n"; log.flush();
        if (initial.info.durationMs > 0) {
            player.setRepeat(true);
            player.seek(initial.info.durationMs - 150, true);
            const auto generation = player.state().generation;
            until(app, [&] { return player.state().generation != generation; }, "Seek probe repeat did not wrap.", 15000);
            QElapsedTimer elapsed; elapsed.start();
            until(app, ready, "Scopes did not recover after repeat.", 15000);
            require(player.state().playing, "Scope preparation stopped repeat playback.");
            log << "Automatic repeat " << QFileInfo(path).suffix() << ": full grid restored in " << elapsed.elapsed() << " ms PASS\n"; log.flush();
        }
    }
}

void checkScopeColors(QApplication &app, const QString &directory) {
    const auto originalPalette = app.palette();
    app.setPalette(QPalette(Qt::white)); // Reproduce light-system popup contrast.
    ScopeWindow scopes;
    PlayerState state; state.valid = true; state.generation = 1;
    state.info.path = "color-preview"; state.info.title = "Color preview"; state.info.voices = {"Pulse 1", "Pulse 2", "Triangle"};
    ScopeFrame frame; frame.generation = 1; frame.mask = 7;
    frame.channels.resize(3);
    for (auto &row : frame.channels) for (int i = 0; i < ScopeFrames; ++i) row.push_back(float((i % 128) - 64) * 100);
    scopes.show(); scopes.present(state, frame); scopes.resize(1000, 680);
    scopes.findChild<QPushButton *>("editLayout")->click(); pump(app, 30);
    auto open = [&](const std::function<void(QDialog *)> &action, int panel = -1) {
        std::exception_ptr error;
        bool opened = false;
        QTimer::singleShot(20, &scopes, [&] {
            auto dialog = scopes.findChild<QDialog *>("scopeColorsDialog");
            opened = true;
            try {
                require(dialog && dialog->isVisible() && dialog->isModal(), "Modal color settings did not open.");
                if (panel >= 0) require(dialog->findChild<QComboBox *>("colorChannel")->currentIndex() == panel, "Pipette selected the wrong backend channel.");
                action(dialog);
            }
            catch (...) { error = std::current_exception(); if (dialog) dialog->reject(); }
        });
        if (panel < 0) scopes.findChild<QPushButton *>("scopeColors")->click();
        else {
            const auto position = scopes.panelRect(panel).topRight() + QPointF(-44, 14);
            QMouseEvent press(QEvent::MouseButtonPress, position, scopes.mapToGlobal(position.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, position, scopes.mapToGlobal(position.toPoint()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&scopes, &press); QApplication::sendEvent(&scopes, &release);
        }
        require(opened, "Pipette click did not open the dialog.");
        if (error) std::rethrow_exception(error);
    };
    auto background = [&] {
        const auto card = scopes.cardRect(0);
        const auto image = scopes.grab().toImage();
        const auto point = (QPointF(card.left() + 4, card.center().y()) * image.devicePixelRatio()).toPoint();
        return image.pixelColor(point);
    };
    auto windowBackground = [&] {
        const auto image = scopes.grab().toImage();
        return image.pixelColor((QPointF(6, 60) * image.devicePixelRatio()).toPoint());
    };
    auto chooseWindowBackground = [&](bool accept) {
        bool hasHex = false;
        QTimer::singleShot(20, &scopes, [&] {
            auto picker = scopes.findChild<QColorDialog *>("windowBackgroundPicker");
            if (!picker) return;
            hasHex = picker->findChild<QLineEdit *>("qt_colorname_lineedit") != nullptr;
            picker->setCurrentColor(QColor(accept ? "#304050" : "#FFFFFF"));
            if (accept) picker->accept(); else picker->reject();
        });
        scopes.findChild<QPushButton *>("scopeWindowColor")->click();
        require(hasHex, "Top toolbar background picker/HEX missing.");
    };
    chooseWindowBackground(true);
    require(windowBackground() == QColor("#304050"), "Window picker did not update canvas painting.");
    chooseWindowBackground(false);
    require(windowBackground() == QColor("#304050"), "Window picker Cancel did not restore background.");
    open([&](QDialog *dialog) {
        auto channel = dialog->findChild<QComboBox *>("colorChannel");
        require(channel->count() == 33 && channel->itemText(32) == "Full mix", "Theme editor must include 32 voice tiles and Full mix.");
        channel->showPopup(); pump(app, 30);
        const auto popup = channel->view()->viewport()->grab().toImage();
        int darkPixels = 0;
        for (int y = 0; y < popup.height(); ++y)
            for (int x = 0; x < popup.width(); ++x)
                if (popup.pixelColor(x, y).lightness() < 100) ++darkPixels;
        require(darkPixels > popup.width() * popup.height() / 2, "Light system palette left the dropdown background unreadable.");
        require(channel->view()->palette().color(QPalette::Text).lightness() > 180, "Dropdown text is not readable on dark background.");
        channel->view()->window()->grab().save(QDir(directory).filePath("native-theme-light-popup.png"));
        channel->hidePopup();
        auto buttons = dialog->findChild<QDialogButtonBox *>();
        auto wave = dialog->findChild<QLineEdit *>("waveformHex");
        wave->setText("#12");
        require(!buttons->button(QDialogButtonBox::Save)->isEnabled() && !dialog->findChild<QPushButton *>("applyColorsToAll")->isEnabled(), "Incomplete HEX can be saved/applied.");
        wave->setText("ff3366");
        dialog->findChild<QLineEdit *>("labelHex")->setText("#FFCC00");
        dialog->findChild<QLineEdit *>("axisHex")->setText("#00FF00");
        dialog->findChild<QLineEdit *>("borderHex")->setText("#00BBEE");
        dialog->findChild<QLineEdit *>("backgroundHex")->setText("#112233");
        require(!dialog->findChild<QLineEdit *>("windowBackgroundHex"), "Window background remains mixed with channel colors.");
        require(background() == QColor("#112233"), "Background HEX did not update actual card paint.");
        const auto cardImage = scopes.grab(scopes.cardRect(0).toRect()).toImage();
        bool greenAxis = false;
        for (int y = 0; y < cardImage.height() && !greenAxis; ++y)
            for (int x = 0; x < cardImage.width(); ++x) {
                const auto c = cardImage.pixelColor(x, y);
                if (c.green() > c.red() + 70 && c.green() > c.blue() + 70) { greenAxis = true; break; }
            }
        require(greenAxis, "Axis HEX did not change the rendered axis.");
        channel->setCurrentIndex(1); require(wave->text() == "#8FB8FF", "Editing one channel changed its neighbor.");
        require(dialog->findChild<QLineEdit *>("axisHex")->text() == "#293E48", "Axis edit changed another channel.");
        channel->setCurrentIndex(0);
        bool pickerHex = false;
        QTimer::singleShot(20, dialog, [&] {
            auto picker = dialog->findChild<QColorDialog *>("scopeColorPicker");
            if (!picker) return;
            pickerHex = picker->findChild<QLineEdit *>("qt_colorname_lineedit") != nullptr;
            picker->setCurrentColor(QColor("#FF3366"));
            picker->grab().save(QDir(directory).filePath("native-color-picker.png"));
            picker->accept();
        });
        dialog->findChild<QPushButton *>("waveformPicker")->click();
        require(pickerHex && wave->text() == "#FF3366", "Color picker/HEX integration failed.");
        dialog->findChild<QPushButton *>("applyColorsToAll")->click(); channel->setCurrentIndex(2);
        require(wave->text() == "#FF3366" && dialog->findChild<QLineEdit *>("backgroundHex")->text() == "#112233", "Apply to all lost selected colors.");
        require(dialog->findChild<QLineEdit *>("axisHex")->text() == "#00FF00", "Apply to all omitted the axis.");
        channel->setCurrentIndex(31);
        require(wave->text() == "#FF3366" && dialog->findChild<QLineEdit *>("axisHex")->text() == "#00FF00", "Apply to all omitted unused theme tiles.");
        channel->setCurrentIndex(2);
        dialog->findChild<QPushButton *>("resetChannelColors")->click();
        require(wave->text() == "#C5ABFF", "Reset channel did not restore its own palette.");
        require(dialog->findChild<QLineEdit *>("axisHex")->text() == "#293E48", "Reset channel did not restore the axis.");
        require(windowBackground() == QColor("#304050"), "Channel reset changed the shared window background.");
        channel->setCurrentIndex(0); require(wave->text() == "#FF3366", "Reset channel changed another channel.");
        dialog->grab().save(QDir(directory).filePath("native-scope-colors.png"));
        scopes.grab().save(QDir(directory).filePath("native-colored-scopes.png"));
        buttons->button(QDialogButtonBox::Save)->click();
    });
    open([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>("backgroundHex")->setText("#FFFFFF");
        dialog->findChild<QLineEdit *>("axisHex")->setText("#ABCDEF");
        dialog->findChild<QPushButton *>("applyColorsToAll")->click(); dialog->reject();
    });
    require(background() == QColor("#112233"), "Cancel did not restore the saved colors.");
    require(windowBackground() == QColor("#304050"), "Cancel did not restore window background.");
    auto channels = scopes.findChild<QListWidget *>("scopeChannelList");
    require(channels->model()->moveRow({}, 1, {}, 0), "Color test could not reorder panels.");
    pump(app, 180);
    const auto order = scopes.visibleChannels();
    open([&](QDialog *dialog) {
        require(dialog->findChild<QLineEdit *>("axisHex")->text() == "#00FF00", "Cancel failed to restore axis after Apply to all.");
        dialog->reject();
    }, 1);
    require(scopes.visibleChannels() == order, "Pipette click hid/reordered a panel.");
    scopes.beginFile(); scopes.present(state, frame);
    require(windowBackground() == QColor("#304050"), "Opening a file lost the window background.");
    require(background() == QColor("#112233"), "Opening a file lost the card colors.");
    state.info.path = "another-file"; state.info.voices.append("Noise");
    scopes.beginFile(); scopes.present(state, frame);
    open([&](QDialog *dialog) {
        dialog->findChild<QComboBox *>("colorChannel")->setCurrentIndex(3);
        require(dialog->findChild<QLineEdit *>("backgroundHex")->text() == "#112233", "New file's extra channel did not inherit the theme.");
        dialog->reject();
    });
    scopes.findChild<QPushButton *>("resetLayout")->click();
    require(background() == QColor("#112233") && windowBackground() == QColor("#304050"), "Layout Reset changed the theme.");

    QTemporaryDir themeDirectory(QDir(directory).absoluteFilePath("theme roundtrip-XXXXXX"));
    require(themeDirectory.isValid(), "Could not create theme check directory.");
    require(QDir().mkpath(playerThemesDirectory()), "Could not create isolated themes directory.");
    QTemporaryFile themeFile(QDir(playerThemesDirectory()).filePath(QString::fromUtf8("Palette Ж-XXXXXX.ini")));
    require(themeFile.open(), "Could not reserve a unique test theme name.");
    const auto themePath = themeFile.fileName(); themeFile.close();
    require(themeFile.remove(), "Could not prepare test theme save.");
    struct ThemeCleanup { QString path; ~ThemeCleanup() { QFile::remove(path); } } cleanup{themePath};
    const auto themeName = QFileInfo(themePath).completeBaseName();
    bool saveOpened = false;
    std::exception_ptr saveError;
    QTimer::singleShot(20, &scopes, [&] {
        auto dialog = scopes.findChild<QDialog *>("saveThemeDialog");
        if (!dialog) return;
        try {
            saveOpened = true;
            require(dialog->isModal() && !qobject_cast<QFileDialog *>(dialog), "Theme save still uses a file chooser.");
            auto name = dialog->findChild<QLineEdit *>("themeName");
            auto save = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
            require(name && dialog->findChildren<QLineEdit *>().size() == 1, "Theme dialog must ask for only a name.");
            for (const auto &invalidName : {"", "   ", "../escape", "CON", "Default", "trailing."}) {
                name->setText(invalidName); require(!save->isEnabled(), "Unsafe or reserved theme name was accepted.");
            }
            name->setText(themeName); require(save->isEnabled() && save->text() == "Save", "Valid Unicode theme name was rejected.");
            dialog->grab().save(QDir(directory).filePath("native-theme-name.png"));
            save->click();
        } catch (...) { saveError = std::current_exception(); dialog->reject(); }
    });
    scopes.findChild<QPushButton *>("saveTheme")->click();
    if (saveError) std::rethrow_exception(saveError);
    require(saveOpened && QFile::exists(themePath), "Save theme UI did not create an INI file.");
    require(QFile::exists(playerSettingsPath()), "Theme selection was not written to the portable settings file.");
    QSettings persisted(playerSettingsPath(), QSettings::IniFormat);
    require(persisted.value("Scopes/Theme").toString() == QFileInfo(themePath).fileName(),
        "Portable settings must store only the theme filename.");
    bool replaceOffered = false;
    QTimer::singleShot(20, &scopes, [&] {
        if (auto dialog = scopes.findChild<QDialog *>("saveThemeDialog")) {
            replaceOffered = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->text() == "Replace";
            dialog->reject();
        }
    });
    scopes.findChild<QPushButton *>("saveTheme")->click();
    require(replaceOffered && QFile::exists(themePath), "Existing theme did not offer replacement or Cancel lost it.");
    ScopeTheme restored; QString error;
    require(restored.load(themePath, error), "Saved theme could not be loaded.");
    require(restored.windowBackground == QColor("#304050"), "Saved theme lost window background.");
    for (int tile = 0; tile < 32; ++tile) {
        const auto &c = restored.tiles[tile];
        if (tile == 2) { require(c.waveform == ScopeColors::defaults(tile).waveform, "Reset tile was not saved."); continue; }
        require(c.waveform == QColor("#FF3366") && c.label == QColor("#FFCC00") && c.axis == QColor("#00FF00") &&
                c.border == QColor("#00BBEE") && c.background == QColor("#112233"), "Theme roundtrip lost a tile color.");
    }
    auto selector = scopes.findChild<QComboBox *>("scopeTheme");
    auto restartTheme = [&](const QString &expectedPath, const QColor &expectedWindow, const QColor &expectedCard) {
        ScopeWindow restarted; restarted.resize(1000, 680); restarted.show(); restarted.present(state, frame);
        require(restarted.findChild<QComboBox *>("scopeTheme")->currentData().toString() == expectedPath, "Startup restored the wrong theme selection.");
        const auto image = restarted.grab().toImage(); const auto card = restarted.cardRect(0);
        require(image.pixelColor((QPointF(6, 60) * image.devicePixelRatio()).toPoint()) == expectedWindow &&
            image.pixelColor((QPointF(card.left() + 4, card.center().y()) * image.devicePixelRatio()).toPoint()) == expectedCard,
            "Startup did not restore actual window/card theme colors.");
        restarted.close();
    };
    restartTheme(themePath, QColor("#304050"), QColor("#112233"));
    auto selectTheme = [&](const QString &path) {
        const int index = selector->findData(path);
        require(index >= 0, "Saved theme not listed in dropdown.");
        selector->setCurrentIndex(index);
        QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, index));
    };
    selectTheme(QString());
    restartTheme(QString(), QColor("#171E24"), QColor("#1B252B"));
    require(background() == QColor("#1B252B") && windowBackground() == QColor("#171E24"), "Default theme did not apply immediately.");
    selectTheme(themePath);
    require(background() == QColor("#112233") && windowBackground() == QColor("#304050"), "Selected theme did not apply immediately.");
    // An unwritable app folder must report the failure, not fall back to a user-profile file.
    const auto settingsPath = playerSettingsPath();
    selectTheme(QString());
    QTemporaryFile blocker(themeDirectory.filePath("blocked-XXXXXX")); require(blocker.open(), "Cannot create settings blocker.");
    app.setProperty("settingsFileForTests", blocker.fileName() + "/BitMusicVisualizer.ini");
    bool settingsWarning = false;
    QTimer::singleShot(20, &scopes, [&] {
        if (auto message = scopes.findChild<QMessageBox *>()) { settingsWarning = message->windowTitle() == "Could not save settings"; message->accept(); }
    });
    selectTheme(themePath);
    app.setProperty("settingsFileForTests", settingsPath);
    require(settingsWarning && background() == QColor("#112233"), "Unwritable settings did not preserve session selection/report failure.");
    restartTheme(QString(), QColor("#171E24"), QColor("#1B252B")); // Failed write left saved selection intact.
    selectTheme(themePath);
    open([&](QDialog *dialog) {
        dialog->findChild<QLineEdit *>("backgroundHex")->setText("#221100"); dialog->accept();
    });
    require(selector->currentData().toString() == ":custom", "Edited theme not marked unsaved.");
    restartTheme(themePath, QColor("#304050"), QColor("#112233"));
    selectTheme(themePath);
    QFile invalid(themePath);
    require(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate), "Could not create invalid theme fixture.");
    invalid.write("[Theme]\nVersion=1\nSlots=32\nWindowBackground=#FFFFFF\n[Tile01]\nWaveform=invalid\n"); invalid.close();
    require(!restored.load(themePath, error) && !error.isEmpty() && restored.windowBackground == QColor("#304050"), "Invalid theme partially changed colors.");
    bool warned = false;
    QTimer::singleShot(20, &scopes, [&] {
        if (auto message = scopes.findChild<QMessageBox *>()) { warned = true; message->accept(); }
    });
    selectTheme(themePath);
    require(warned && background() == QColor("#112233") && windowBackground() == QColor("#304050"), "Invalid theme selection changed current appearance.");
    restartTheme(QString(), QColor("#171E24"), QColor("#1B252B")); // Corrupt at next startup.
    require(restored.save(themePath, error), "Could not restore the test theme.");
    selectTheme(themePath);
    require(QFile::remove(themePath), "Could not remove the selected test theme.");
    restartTheme(QString(), QColor("#171E24"), QColor("#1B252B")); // Missing at next startup.
    require(!restored.save(themeDirectory.filePath("missing/subfolder/theme.ini"), error) && !error.isEmpty(), "Theme save failure was not reported.");
    scopes.grab().save(QDir(directory).filePath("native-theme-editor.png"));
    scopes.resize(480, 360); pump(app, 20);
    scopes.grab().save(QDir(directory).filePath("native-colors-editor-small.png"));
    scopes.close();
    app.setPalette(originalPalette);
}
void checkWindowSettings(QApplication &app, const QString &directory, QTextStream &log) {
    const auto previousPath = app.property("settingsFileForTests");
    QTemporaryDir isolated; require(isolated.isValid(), "Cannot isolate window settings.");
    app.setProperty("settingsFileForTests", isolated.filePath("windows.ini"));
    try {
        { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
          ini.setValue("Effects/GlowBrightness", 77); ini.setValue("Other/Keep", "untouched"); ini.sync(); }
        const auto available = app.primaryScreen()->availableGeometry();
        QRect mainRect, scopeRect;
        {
            PlayerWindow first; first.player().setVolume(0); first.show(); first.scopeWindow().show();
            first.setGeometry(QRect(available.topLeft()+QPoint(30,60), QSize(420,440)));
            first.scopeWindow().setGeometry(QRect(available.topLeft()+QPoint(80,100), QSize(720,480))); pump(app, 60);
            auto &scopes = first.scopeWindow();
            auto sizeButton = scopes.findChild<QPushButton *>("scopeWindowSize");
            require(sizeButton && !sizeButton->isVisible(), "Window size must be available only in edit mode.");
            scopes.findChild<QPushButton *>("editLayout")->click(); pump(app, 30);
            const auto dpr = scopes.devicePixelRatioF();
            auto width = scopes.findChild<QSpinBox *>("scopePixelWidth");
            auto height = scopes.findChild<QSpinBox *>("scopePixelHeight");
            require(sizeButton->text().isEmpty() && width && height && !width->isVisible(), "Size fields should start collapsed behind an icon.");
            auto background = scopes.findChild<QPushButton *>("scopeWindowColor");
            require(sizeButton->parentWidget() == background->parentWidget() && sizeButton->x() > background->geometry().right(),
                    "Window size icon is not next to the background pipette.");
            scopes.resize(1120, 480); pump(app, 30);
            const auto compactHeight = scopes.findChild<QWidget *>("scopeEditor")->height();
            sizeButton->click(); pump(app, 30);
            require(scopes.findChild<QWidget *>("scopeEditor")->height() == compactHeight,
                    "Expanding dimensions added a dedicated toolbar row.");
            scopes.grab().save(QDir(directory).filePath("native-window-size-wide.png"));
            sizeButton->click(); scopes.resize(720, 480); pump(app, 30);
            const auto beforeExpand = scopes.geometry();
            sizeButton->click(); pump(app, 30);
            require(width->isVisible() && height->isVisible() && !QApplication::activeModalWidget() && scopes.geometry() == beforeExpand,
                    "Size fields did not expand inline without changing the window.");
            require(QSize(width->value(), height->value()) == scopes.size()*dpr, "Inline fields do not show screen pixels.");
            auto typeKey = [&](int key, const QString &text = QString()) {
                QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
                QApplication::sendEvent(width, &press); pump(app, 20);
            };
            width->setFocus(); width->selectAll();
            for (const auto character : QString("128")) typeKey(character.unicode(), QString(character));
            require(scopes.geometry() == beforeExpand, "Incomplete number changed the window size.");
            typeKey(Qt::Key_2, "2");
            require(width->value() == 1282 && width->findChild<QLineEdit *>()->text() == "1282 px" &&
                    scopes.width() == qRound(1282/dpr), "Typing did not resize live or resizing overwrote the unfinished input.");
            typeKey(Qt::Key_Up);
            require(width->value() == 1283 && scopes.width() == qRound(1283/dpr), "Stepping did not resize immediately.");
            typeKey(Qt::Key_Return);
            require(width->value() == qRound(scopes.width()*dpr), "Finishing input did not normalize to actual pixels.");
            const auto beforeCollapse = scopes.geometry();
            sizeButton->click(); pump(app, 30);
            require(!width->isVisible() && scopes.geometry() == beforeCollapse, "Collapsing fields changed the window size.");
            scopes.grab().save(QDir(directory).filePath("native-window-size-collapsed.png"));
            const QSize requested(qRound(720*dpr)+1, qRound(480*dpr)+1);
            scopes.showMaximized(); pump(app, 60);
            sizeButton->click(); pump(app, 30);
            require(scopes.isMaximized(), "Opening size controls left maximized mode.");
            width->setValue(requested.width()); height->setValue(requested.height()); pump(app, 60);
            require(!scopes.isMaximized(), "Changing a dimension did not leave maximized mode.");
            const QSize expected(qRound(requested.width()/dpr), qRound(requested.height()/dpr));
            require(scopes.size() == expected && scopes.grab().size() == expected*dpr,
                    "Requested screen-pixel size did not reach the rendered window.");
            require(scopes.findChild<QLabel *>("scopeSizeRounding")->text().isEmpty() == (expected*dpr == requested),
                    "Rounded live dimensions need an actual-size readout.");
            scopes.grab().save(QDir(directory).filePath("native-window-size-editor.png"));
            width->setValue(1); height->setValue(1); pump(app, 30);
            require(scopes.size() == scopes.minimumSize() && QSize(width->value(), height->value()) == scopes.minimumSize()*dpr,
                    "Inline size minimum does not account for display scaling.");
            auto editor = scopes.findChild<QWidget *>("scopeEditor");
            QVector<QRect> controls;
            for (auto name : {"scopeWindowSize", "scopePixelWidth", "scopePixelHeight", "gridRows", "gridColumns", "channelNumbers", "channelNames", "scopeStereo", "keepGrid", "scopeWindowColor", "resetLayout", "applyLayout"}) {
                auto control = editor->findChild<QWidget *>(name);
                require(control && control->isVisible(), "A toolbar control disappeared at the minimum window size.");
                const QRect bounds(control->mapTo(editor, QPoint()), control->size());
                require(editor->rect().contains(bounds), "Minimum-size editor clips a toolbar control.");
                for (const auto &other : controls) require(!bounds.intersects(other), "Minimum-size toolbar controls overlap.");
                controls << bounds;
            }
            scopes.grab().save(QDir(directory).filePath("native-window-size-small.png"));
            const int sizeY = sizeButton->mapTo(editor, sizeButton->rect().center()).y();
            require(std::abs(width->mapTo(editor, width->rect().center()).y()-sizeY) <= 2 &&
                    std::abs(height->mapTo(editor, height->rect().center()).y()-sizeY) <= 2 &&
                    width->mapTo(editor, QPoint()).x() > sizeButton->mapTo(editor, sizeButton->rect().topRight()).x(),
                    "Size fields did not expand sideways in the icon's row.");
            scopes.resize(expected); pump(app, 30);
            require(QSize(width->value(), height->value()) == expected*dpr, "External resizing did not update inline dimensions.");
            mainRect = first.geometry(); scopeRect = first.scopeWindow().geometry();
            first.scopeWindow().close(); require(first.isVisible(), "Scope close stopped player while saving layout.");
            first.close();
        }
        { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
          require(!ini.value("Windows/Player").toByteArray().isEmpty() && !ini.value("Windows/Scopes").toByteArray().isEmpty() &&
              ini.value("Effects/GlowBrightness").toInt() == 77 && ini.value("Other/Keep").toString() == "untouched",
              "Window save lost portable settings or hidden scope geometry."); }
        {
            PlayerWindow restored; restored.show(); restored.scopeWindow().show(); pump(app, 60);
            require(restored.geometry() == mainRect && restored.scopeWindow().geometry() == scopeRect, "Window positions/sizes did not roundtrip.");
            restored.showMaximized(); restored.scopeWindow().showMaximized(); pump(app, 60); restored.close();
        }
        {
            PlayerWindow maximized; maximized.show(); maximized.scopeWindow().show(); pump(app, 60);
            require(maximized.isMaximized() && maximized.scopeWindow().isMaximized(), "Maximized window state was not restored.");
            maximized.showNormal(); maximized.scopeWindow().showNormal(); pump(app, 60);
            require(maximized.geometry() == mainRect && maximized.scopeWindow().geometry() == scopeRect, "Maximize lost normal window positions/sizes.");
            maximized.showMinimized(); maximized.scopeWindow().showMinimized(); pump(app, 60); maximized.close();
        }
        {
            PlayerWindow minimized; minimized.show(); minimized.scopeWindow().show(); pump(app, 60);
            require(!minimized.isMinimized() && !minimized.scopeWindow().isMinimized() && minimized.geometry() == mainRect &&
                minimized.scopeWindow().geometry() == scopeRect, "Minimized exit lost geometry or restored hidden taskbar state.");
            minimized.close();
        }
        // Simulate stale coordinates from a removed monitor, without changing the OS display setup.
        { QWidget outside(nullptr, Qt::Window | Qt::FramelessWindowHint);
          outside.setGeometry(-10000, -10000, 720, 480);
          QSettings ini(playerSettingsPath(), QSettings::IniFormat);
          ini.setValue("Windows/Player", outside.saveGeometry()); ini.setValue("Windows/Scopes", outside.saveGeometry()); ini.sync(); }
        {
            PlayerWindow offscreen; offscreen.show(); offscreen.scopeWindow().show(); pump(app, 60);
            for (QWidget *window : {static_cast<QWidget *>(&offscreen), static_cast<QWidget *>(&offscreen.scopeWindow())})
                require(window->screen()->availableGeometry().contains(window->frameGeometry()), "Restored window is lost outside the screen.");
            offscreen.close();
        }
        { QSettings ini(playerSettingsPath(), QSettings::IniFormat);
          ini.setValue("Windows/Player", QByteArray("invalid")); ini.remove("Windows/Scopes"); ini.sync(); }
        {
            PlayerWindow invalid;
            require(invalid.size() == QSize(430,430) && invalid.scopeWindow().size() == QSize(1120,700), "Invalid/missing geometry did not keep defaults.");
        }
        const auto path = playerSettingsPath(); app.setProperty("settingsFileForTests", path + "/blocked.ini");
        {
            PlayerWindow blocked; blocked.show(); bool warned = false;
            QTimer dismiss;
            QObject::connect(&dismiss, &QTimer::timeout, &blocked, [&] {
                if (auto message = blocked.findChild<QMessageBox *>()) { warned = message->text().contains(path); message->accept(); }
            });
            dismiss.start(20); blocked.close();
            require(warned && !blocked.isVisible() && !blocked.player().isRunning(), "Window save failure blocked exit or hid the error.");
        }
        log << "Window settings: inline icon toggle, live typing/stepping, incomplete input, maximize, DPI rounding, external resize, narrow toolbar, both positions/sizes, hidden scopes, normal/maximized/minimized exit, offscreen recovery, corrupt defaults and save failure PASS\n"; log.flush();
    } catch (...) { app.setProperty("settingsFileForTests", previousPath); throw; }
    app.setProperty("settingsFileForTests", previousPath);
}
void checkShutdown(QApplication &app, QStringList paths, QTextStream &log) {
    paths.prepend(QString()); // Even an idle scope worker used to delay exit.
    for (const auto &path : paths) {
        for (const QString mode : {"playing", "hidden", "paused", "stopped", "seeking"}) {
            auto window = std::make_unique<PlayerWindow>();
            auto &player = window->player(); player.setVolume(0); window->show();
            if (!path.isEmpty()) {
                window->loadFile(path);
                until(app, [&] { return !player.state().busy && player.state().positionMs > 100 && player.scopes().mask; }, "Shutdown fixture failed.");
                if (mode == "hidden") {
                    window->scopeWindow().close();
                    until(app, [&] { return player.state().scopesSuspended; }, "Scope worker did not suspend.");
                    require(player.isRunning() && player.state().playing && window->isVisible(), "Scope close stopped playback.");
                }
                if (mode == "paused" || mode == "stopped") {
                    player.seek(mode == "paused" ? 100 : 0, false);
                    until(app, [&] { return !player.state().busy; }, "Pause/Stop failed.");
                }
                if (mode == "seeking") {
                    player.seek(3600000, true);
                    pump(app, 8); // Allow the decoder/scope workers to begin preparation.
                }
            }
            QElapsedTimer elapsed; elapsed.start();
            window->findChild<QPushButton *>("windowClose")->click();
            require(!player.isRunning() && !window->isVisible() && !window->scopeWindow().isVisible(), "Close left workers or windows alive.");
            player.shutdown(); // Repeated shutdown and destructors must also return promptly.
            window.reset();
            log << "Shutdown " << (path.isEmpty() ? "empty" : QFileInfo(path).fileName() + " / " + mode)
                << ": " << elapsed.elapsed() << " ms\n"; log.flush();
            require(elapsed.elapsed() < 750, "Shutdown exceeded 750 ms.");
            if (path.isEmpty()) break;
        }
    }
    log << "Main close latency, complete worker shutdown and scope-only close PASS\n"; log.flush();
}
}

int runChecks(QApplication &app, const QStringList &arguments) {
    app.setQuitOnLastWindowClosed(false);
    const int option = arguments.indexOf("--self-test");
    if (option + 1 >= arguments.size()) return 2;
    const QString directory = arguments[option + 1];
    QFile report(QDir(directory).filePath("native-checks.txt"));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) return 3;
    QTextStream log(&report);
    // Never read or overwrite the user's last theme while running automated checks.
    QTemporaryDir settingsDirectory(QDir(directory).absoluteFilePath("settings-XXXXXX"));
    if (!settingsDirectory.isValid()) return 4;
    app.setProperty("settingsFileForTests", settingsDirectory.filePath("BitMusicVisualizer.ini"));
    app.setProperty("themesDirectoryForTests", settingsDirectory.filePath("themes"));
    try {
        if (arguments.contains("--views-only")) { checkViews(app, directory, log); return 0; }
        if (arguments.contains("--layout-only")) { checkLayoutSpans(app, directory, log); return 0; }
        if (arguments.contains("--fps-only")) { checkFrameRates(app, directory, log); return 0; }
        if (arguments.contains("--waves-only")) {
            QStringList paths;
            for (int i = option + 2; i < arguments.size(); ++i) if (!arguments[i].startsWith('-')) paths << arguments[i];
            checkWaves(app, directory, log, paths); return 0;
        }
        if (arguments.contains("--settings-only")) { checkWindowSettings(app, directory, log); return 0; }
        if (arguments.contains("--shutdown-only")) {
            QStringList paths;
            for (const auto &extension : {"nsf", "vgz", "spc"}) paths << QDir(directory).filePath(QString("demo.%1").arg(extension));
            for (int i = option + 2; i < arguments.size(); ++i)
                if (!arguments[i].startsWith('-')) paths << arguments[i];
            checkShutdown(app, paths, log);
            return 0;
        }
        if (arguments.contains("--gbs-only")) { checkGbs(app, directory, log); return 0; }
        if (arguments.contains("--ay-only")) { checkAy(app, directory, log); return 0; }
        if (arguments.contains("--stereo-only")) {
            checkStereoOutputs(app, directory);
            log << "Stereo/mono capability, output PCM isolation, lane dimming, controls and transport preservation PASS\n";
            return 0;
        }
        checkScopeColors(app, directory);
        log << "Scope themes: light-system popup contrast, 32-tile HEX/picker, unused tiles, reorder/file retention, INI roundtrip, immediate selection, invalid file rollback, Save/Cancel, startup restore/Default/missing/corrupt/unsaved behavior and layout Reset PASS\n"; log.flush();
        if (arguments.contains("--colors-only")) return 0;
        checkWindowSettings(app, directory, log);
        QStringList seekPaths;
        for (int i = option + 2; i < arguments.size(); ++i)
            if (!arguments[i].startsWith('-')) seekPaths << arguments[i];
        if (arguments.contains("--scope-seek-only")) {
            checkScopeSeeks(app, seekPaths, log);
            log << "Scope seek checks PASS\n";
            return 0;
        }
        checkGbs(app, directory, log);
        checkAy(app, directory, log);
        checkWaves(app, directory, log);
        checkFrameRates(app, directory, log);
        checkLayoutSpans(app, directory, log);
        checkViews(app, directory, log);
        checkStereoOutputs(app, directory);
        log << "Stereo/mono capability, output PCM isolation, lane dimming, controls and transport preservation PASS\n"; log.flush();
        checkRing(); log << "Concurrent PCM ring: 500000 samples, variable callback sizes, exact order PASS\n"; log.flush();
        QStringList paths;
        for (const auto &extension : {"nsf", "vgm", "spc", "vgz"}) paths << QDir(directory).filePath(QString("demo.%1").arg(extension));
        std::array<short, BlockFrames * 2> output{}, reference{};
        for (const auto &path : paths) {
            GmeTrack track(path);
            track.render(output.data(), BlockFrames); require(peak(output) > 100, "Fixture master silent.");
            track.seek(2 * SampleRate); require(track.frame() == 2 * SampleRate, "Forward seek mismatch.");
            track.seek(SampleRate / 4); track.render(output.data(), BlockFrames);
            GmeTrack fresh(path); fresh.seek(SampleRate / 4); fresh.render(reference.data(), BlockFrames);
            require(output == reference, "Backward seek does not reproduce a fresh render.");
            track.mute(0xffffffffu);
            for (int i = 0; i < 50; ++i) track.render(output.data(), BlockFrames);
            require(peak(output) <= 1, "Muting all voices failed.");
            track.mute(0); for (int i = 0; i < 5; ++i) track.render(output.data(), BlockFrames);
            require(peak(output) > 100, "Unmuting voices failed.");
            // Every fixture uses voice zero. Soloing an unused voice must be silent.
            GmeTrack solo(path); solo.dry(); solo.mute(~1u);
            for (int i = 0; i < 8; ++i) solo.render(output.data(), BlockFrames);
            require(peak(output) > 100, "Solo active voice failed.");
            solo.mute(~2u); for (int i = 0; i < 50; ++i) solo.render(output.data(), BlockFrames);
            require(peak(output) <= 1, "Unused solo voice is not silent.");
            // Rewinding resets the chip as well as its clock. A solo must stay a
            // solo even for short seeks that do not use libgme's fast skip path.
            solo.seek(0);
            solo.seek(SampleRate / 4);
            for (int i = 0; i < 8; ++i) solo.render(output.data(), BlockFrames);
            log << "Rewound unused " << QFileInfo(path).suffix() << " solo peak=" << peak(output) << '\n'; log.flush();
            require(peak(output) <= 1, "Backward seek lost solo isolation.");
            solo.seek(3 * SampleRate);
            solo.render(output.data(), BlockFrames);
            require(peak(output) <= 1, "Forward seek lost solo isolation.");
            log << "Engine " << QFileInfo(path).suffix() << ": master, mute, solo, seek PASS\n"; log.flush();
        }
        checkScopeSeeks(app, paths + seekPaths, log, paths.size());
        bool invalid = false;
        try { GmeTrack bad(paths[0], 999); } catch (const std::exception &) { invalid = true; }
        require(invalid, "Invalid subsong accepted.");
        GmeTrack unnamed(paths[0]);
        require(unnamed.info().playlist.size() == 2 && unnamed.info().durationMs < 0, "NSF unknown duration/list mismatch.");
        require(unnamed.info().playlist[1].title == QString::fromUtf8("Track 02"), "Unnamed subsong fallback missing.");
        const auto namedPath = QDir(directory).filePath("named.nsfe");
        GmeTrack named(namedPath);
        require(named.info().playlist.size() == 3 && named.info().playlist[1].title == "Triangle Interlude", "NSFE track titles missing.");
        require(named.info().playlist[0].durationMs == 4000 && named.info().playlist[2].durationMs == 2000, "NSFE track durations missing.");
        const auto loopPath = QDir(directory).filePath("intro-loop.vgm");
        GmeTrack loop(loopPath);
        require(loop.info().durationMs == 2000 && loop.info().loopStartMs == 1000, "VGM intro/loop duration not recognized.");
        { ScopeWindow metadataProbe; PlayerState metadata; metadata.valid = true;
          metadata.info = GmeTrack(paths[2]).info(); metadataProbe.present(metadata, {});
          metadata.busy = true; metadata.info = unnamed.info(); metadataProbe.present(metadata, {});
          require(metadataProbe.visibleChannels().size() == 5 && metadataProbe.findChild<QListWidget *>("scopeChannelList")->count() == 6, "Busy metadata switch retained out-of-range channels.");
          require(!metadataProbe.grab().isNull(), "Busy metadata switch failed to paint."); }
        PlayerWindow window;
        window.player().setVolume(0);
        window.show();
        require(window.width() <= 450 && window.height() <= 460, "Player no longer compact.");
        require(window.findChildren<QLineEdit *>().isEmpty() && window.findChildren<QSpinBox *>().isEmpty(), "Removed time/range controls remain.");
        require(window.windowFlags().testFlag(Qt::FramelessWindowHint) && window.scopeWindow().windowFlags().testFlag(Qt::FramelessWindowHint), "A native title bar remains.");
        require(!window.windowIcon().pixmap(32, 32).isNull() && !window.scopeWindow().windowIcon().isNull(), "Application icon missing.");
        for (const auto &path : paths) {
            window.loadFile(path);
            until(app, [&] { auto s = window.player().state(); return !s.busy && s.playing && s.positionMs > 250; }, "Playback did not start.");
            until(app, [&] { auto s = window.player().state(); auto f = window.player().scopes(); return f.generation == s.generation && (f.mask & 1) && f.positionMs > 100; }, "Channel scopes missing.");
            auto s = window.player().state();
            auto scope = window.player().scopes();
            require(scope.channels.size() == s.info.voices.size(), "Scope channel mapping mismatch.");
            require(std::any_of(scope.channels[0].begin(), scope.channels[0].end(), [](float v) { return std::abs(v) > 100; }), "Active scope is silent.");
            window.findChild<QToolButton *>("voice0")->click();
            require(window.player().state().muteMask & 1, "UI mute did not apply.");
            window.findChild<QToolButton *>("voice0")->click();
            window.findChild<QPushButton *>("primary")->click();
            until(app, [&] { return !window.player().state().busy; }, "Pause stuck.");
            const auto paused = window.player().state(); pump(app, 120);
            require(!paused.playing && paused.positionMs == window.player().state().positionMs, "Pause did not freeze clock.");
            window.player().seek(2000, false);
            until(app, [&] { return !window.player().state().busy; }, "Paused seek stuck.");
            require(!window.player().state().playing && window.player().state().positionMs == 2000, "Paused seek changed transport state.");
            until(app, [&] { return window.findChild<QPushButton *>("primary")->isEnabled(); }, "Resume control remained disabled.");
            window.findChild<QPushButton *>("primary")->click();
            until(app, [&] { return window.player().state().positionMs > 2150 && !window.player().state().busy; }, "Resume failed.");
            window.player().seek(500, true);
            until(app, [&] { auto s2 = window.player().state(); return !s2.busy && s2.positionMs > 600 && s2.positionMs < 1500; }, "Playing seek failed.");
            window.findChild<QPushButton *>("stop")->click();
            until(app, [&] { return !window.player().state().busy; }, "Stop stuck.");
            require(!window.player().state().playing && window.player().state().positionMs == 0, "Stop did not reset.");
            window.refresh();
            auto timeline = window.findChild<QSlider *>("timeline");
            const auto generation = window.player().state().generation;
            const QPointF point(timeline->width() * .01 + 7, timeline->height() / 2.0);
            QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &press);
            require(window.player().state().generation == generation, "Timeline seeks before pointer release.");
            const int selected = timeline->value();
            QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &release);
            until(app, [&] { return !window.player().state().busy; }, "Pointer seek stuck.");
            require(!window.player().state().playing && std::abs(window.player().state().positionMs - selected) <= 1, "Pointer seek did not preserve paused position.");
            log << "UI " << QFileInfo(path).suffix() << ": audio, scopes, mute, pause, seeks, Stop PASS\n"; log.flush();
        }
        window.loadFile(paths[0], 1);
        until(app, [&] { return !window.player().state().busy && window.player().state().positionMs > 200; }, "Subsong did not start.");
        require(window.player().state().info.song == 1, "Subsong mismatch.");
        window.player().seek(60000, true); window.loadFile(paths[2]);
        until(app, [&] { auto s = window.player().state(); return !s.busy && s.info.path == paths[2] && s.positionMs > 200 && s.positionMs < 2000; }, "Cancelled seek leaked into replacement file.");
        window.refresh();
        auto list = window.findChild<QListWidget *>("songs");
        require(list->count() == 1, "Single-song file not shown as a one-item playlist.");
        window.loadFile(namedPath);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.path == namedPath; }, "Named file failed.");
        window.refresh();
        require(list->count() == 3 && list->item(1)->text() == "Triangle Interlude", "Named playlist UI mismatch.");
        list->setCurrentRow(1); pump(app, 60);
        require(window.player().state().info.song == 0, "Single selection started playback.");
        const QPointF itemPoint = list->visualItemRect(list->item(1)).center();
        QMouseEvent rowPress(QEvent::MouseButtonPress, itemPoint, itemPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent rowRelease(QEvent::MouseButtonRelease, itemPoint, itemPoint, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QMouseEvent rowDouble(QEvent::MouseButtonDblClick, itemPoint, itemPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &rowPress); QApplication::sendEvent(list->viewport(), &rowRelease);
        QApplication::sendEvent(list->viewport(), &rowDouble); QApplication::sendEvent(list->viewport(), &rowRelease);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 1; }, "Double-click did not play the chosen subsong.");
        window.refresh(); window.findChild<QPushButton *>("next")->click();
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 2; }, "Next button failed.");
        window.refresh(); require(!window.findChild<QPushButton *>("next")->isEnabled(), "Next is enabled after last track.");
        window.findChild<QPushButton *>("previous")->click();
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 1; }, "Previous button failed.");
        window.player().seek(2800, true);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 2; }, "Known end did not advance to next track.");
        window.player().seek(1800, true);
        until(app, [&] { auto s = window.player().state(); return !s.busy && !s.playing && s.positionMs == 2000; }, "Last track did not stop at known end.");
        window.refresh(); window.findChild<QPushButton *>("primary")->click();
        until(app, [&] { auto s = window.player().state(); return !s.busy && s.playing && s.positionMs > 100 && s.positionMs < 1000; }, "Play after EOF did not restart.");
        window.loadFile(loopPath);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.path == loopPath; }, "Loop fixture failed.");
        window.refresh(); auto repeat = window.findChild<QPushButton *>("repeat"); repeat->click();
        require(window.player().state().repeat, "Repeat toggle did not enable.");
        window.player().seek(1800, true);
        until(app, [&] { auto s = window.player().state(); return !s.busy && s.playing && s.positionMs >= 1000 && s.positionMs < 1600; }, "Repeat did not return to loop start.");
        for (int repetition = 0; repetition < 3; ++repetition) {
            const auto generation = window.player().state().generation;
            until(app, [&] { return window.player().state().generation != generation; }, "Repeat transition missing.", 2000);
            until(app, [&] { auto s = window.player().state(); auto f = window.player().scopes();
                return !s.busy && f.generation == s.generation && f.mask == 255 && s.positionMs - f.positionMs < 200;
            }, "Scopes stuck Preparing after automatic repeat.", 500);
            const auto f = window.player().scopes();
            require(std::any_of(f.channels[0].begin(), f.channels[0].end(), [](float v) { return std::abs(v) > 100; }), "Repeat lost active waveform.");
            require(std::all_of(f.channels[1].begin(), f.channels[1].end(), [](float v) { return std::abs(v) <= 1; }), "Repeat lost channel isolation.");
        }
        log << "Three automatic VGM loops: full grid ready within 500 ms; active/unused channel isolation PASS\n"; log.flush();
        window.refresh(); repeat->click(); require(!window.player().state().repeat, "Repeat toggle did not disable.");
        until(app, [&] { auto s = window.player().state(); return !s.busy && !s.playing && s.positionMs == 2000; }, "Disabling repeat did not stop at loop end.");
        window.loadFile(paths[0]);
        until(app, [&] { return !window.player().state().busy; }, "Unknown duration load failed.");
        window.player().seek(181000, true);
        until(app, [&] { auto s = window.player().state(); return !s.busy && s.playing && s.positionMs > 181100; }, "Unknown-duration track stopped at invented endpoint.");
        window.refresh(); require(!repeat->isEnabled() && !window.player().state().repeat, "Unknown-duration repeat state is misleading.");
        require(window.findChild<QSlider *>("timeline")->maximum() > 181000, "Unknown-duration timeline did not expand.");
        log << "Compact UI: metadata playlist, double-click, previous/next, auto-next, final stop, repeat on/off, unknown duration PASS\n"; log.flush();
        auto scopeToggle = window.findChild<QPushButton *>("showScopes");
        auto &scopes = window.scopeWindow();
        require(scopeToggle->isChecked(), "Visible scopes toggle is not checked.");
        scopeToggle->click();
        until(app, [&] { return window.player().state().scopesSuspended; }, "Hidden scope worker did not suspend.");
        auto hidden = window.player().state(); pump(app, 450);
        require(!scopes.isVisible() && !scopeToggle->isChecked(), "Scope toggle did not hide window.");
        require(window.player().state().scopeBlocks == hidden.scopeBlocks && window.player().scopes().channels.isEmpty(), "Hidden scopes still render or retain stale snapshots.");
        require(window.player().state().positionMs > hidden.positionMs + 300, "Hiding scopes stopped music.");
        // Use an unknown-duration fixture here: waiting for cold scope emulators
        // must not race the six-second VGM's natural end during lifecycle checks.
        window.loadFile(paths[2]);
        until(app, [&] { return !window.player().state().busy; }, "Hidden file switch failed.");
        window.player().seek(1000, true);
        until(app, [&] { return !window.player().state().busy && window.player().state().positionMs > 1100; }, "Hidden seek failed.");
        require(!scopes.isVisible() && window.player().state().scopeBlocks == hidden.scopeBlocks, "File/seek reopened hidden scopes.");
        scopeToggle->click();
        require(window.player().scopes().channels.isEmpty(), "Reopening exposed stale scope history.");
        until(app, [&] { auto s = window.player().state(); auto f = window.player().scopes(); return f.generation == s.generation && f.mask && std::abs(s.positionMs - f.positionMs) < 300; }, "Reopened scopes did not synchronize.");
        require(scopeToggle->isChecked() && scopes.isVisible(), "Scope toggle did not restore window.");
        scopes.findChild<QPushButton *>("windowClose")->click();
        until(app, [&] { return window.player().state().scopesSuspended; }, "Scope close did not suspend computation.");
        const auto afterScopeClose = window.player().state();
        if (!afterScopeClose.playing) log << "Scope close state: position=" << afterScopeClose.positionMs << ", error=" << afterScopeClose.error << '\n';
        require(window.isVisible() && window.player().isRunning() && afterScopeClose.playing && !scopes.isVisible(), "Scope close shut down player.");
        scopeToggle->click(); pump(app, 60);
        scopes.findChild<QPushButton *>("windowMinimize")->click();
        until(app, [&] { return scopes.isMinimized() && window.player().state().scopesSuspended; }, "Minimized scopes still compute.");
        require(!scopeToggle->isChecked(), "Minimized scope toggle remained checked.");
        scopeToggle->click();
        until(app, [&] { return scopes.renderingVisible() && !window.player().state().scopesSuspended; }, "Scope restore failed.");
        for (FrameWindow *frame : {static_cast<FrameWindow *>(&window), static_cast<FrameWindow *>(&scopes)}) {
            auto maximize = frame->findChild<QPushButton *>("windowMaximize"); maximize->click(); pump(app, 60);
            require(frame->isMaximized(), "Custom maximize failed.");
            maximize->click(); pump(app, 60); require(!frame->isMaximized(), "Custom restore failed.");
            auto bar = frame->findChild<QWidget *>("titleBar");
            const QPointF titlePoint(60, 15);
            QMouseEvent doubleTitle(QEvent::MouseButtonDblClick, titlePoint, bar->mapToGlobal(titlePoint.toPoint()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &doubleTitle); pump(app, 40); require(frame->isMaximized(), "Title double-click did not maximize.");
            QApplication::sendEvent(bar, &doubleTitle); pump(app, 40); require(!frame->isMaximized(), "Title double-click did not restore.");
            const QPointF corner(2, 2);
            QMouseEvent hover(QEvent::MouseMove, corner, frame->mapToGlobal(corner.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(frame, &hover); require(frame->cursor().shape() == Qt::SizeFDiagCursor, "Frameless resize grip missing.");
        }
        window.findChild<QPushButton *>("windowMinimize")->click(); pump(app, 60);
        require(window.isMinimized() && window.player().state().playing, "Main minimize interrupted playback.");
        window.showNormal(); pump(app, 60);
        log << "Window lifecycle: frameless controls, icons, scope toggle/close/minimize, worker suspension, hidden file/seek, fresh resume PASS\n"; log.flush();
        window.loadFile(paths[0]);
        until(app, [&] { return !window.player().state().busy && window.player().state().positionMs > 100; }, "Layout fixture failed.");
        window.refresh();
        auto edit = scopes.findChild<QPushButton *>("editLayout"); edit->click(); pump(app, 50);
        auto rows = scopes.findChild<QSpinBox *>("gridRows");
        auto columns = scopes.findChild<QSpinBox *>("gridColumns");
        auto keepGrid = scopes.findChild<QCheckBox *>("keepGrid");
        auto numbers = scopes.findChild<QCheckBox *>("channelNumbers");
        auto names = scopes.findChild<QCheckBox *>("channelNames");
        auto visibility = scopes.findChild<QListWidget *>("scopeChannelList");
        require(edit->isChecked() && edit->toolTip() == "Done" && rows->isVisible() && visibility->count() == 6, "Editor controls missing.");
        rows->setValue(2); columns->setValue(2);
        require(scopes.visibleChannels() == QVector<int>({0, 1, 2, 3}), "Grid overflow is not clipped in channel order.");
        until(app, [&] { auto f = window.player().scopes(); return f.generation == window.player().state().generation && f.mask == 15; }, "Visible-only scope mask did not apply.");
        require(window.player().scopes().channels[4].isEmpty(), "Overflow channel still rendered.");
        const auto muteBefore = window.player().state().muteMask;
        auto mouse = [&](QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, scopes.mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&scopes, &event);
        };
        const auto hidePoint = scopes.cardRect(1).topRight() + QPointF(-15, 14);
        mouse(QEvent::MouseButtonPress, hidePoint, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, hidePoint, Qt::LeftButton, Qt::NoButton);
        require(scopes.visibleChannels() == QVector<int>({0, 2, 3, 4}), "Hidden panel did not refill from overflow.");
        require(window.player().state().muteMask == muteBefore && window.player().state().playing, "Hiding a waveform changed audio.");
        until(app, [&] { return window.player().scopes().mask == 29; }, "Hidden channel continued rendering.");
        require(window.player().scopes().channels[1].isEmpty(), "Hidden scope samples retained.");
        const auto from = scopes.cardRect(0).topLeft() + QPointF(15, 15);
        const auto to = scopes.cardRect(2).topLeft() + QPointF(45, 35);
        const auto sourceRect = scopes.cardRect(0), neighborRect = scopes.cardRect(1);
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        require(scopes.panelRect(0) == sourceRect.translated(to - from), "Dragged panel does not follow the pointer.");
        require(scopes.visibleChannels() == QVector<int>({0, 2, 3, 4}), "Drag preview committed channel order before release.");
        pump(app, 60);
        scopes.grab().save(QDir(directory).filePath("native-editor-drag.png"));
        require(scopes.panelRect(2) != neighborRect, "Neighbor panel did not animate toward its new slot.");
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        require(scopes.visibleChannels() == QVector<int>({2, 3, 0, 4}), "Grid drag did not reorder compactly.");
        require(scopes.panelRect(0) != scopes.cardRect(2), "Dropped panel jumped instead of settling into its slot.");
        pump(app, 220); require(scopes.panelRect(0) == scopes.cardRect(2), "Dropped panel failed to settle into its slot.");
        const auto cancelFrom = scopes.cardRect(1).topLeft() + QPointF(15, 15);
        const auto cancelTo = QPointF(scopes.width() - 30, 150);
        mouse(QEvent::MouseButtonPress, cancelFrom, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, cancelTo, Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, cancelTo, Qt::LeftButton, Qt::NoButton);
        pump(app, 220);
        require(scopes.visibleChannels() == QVector<int>({2, 3, 0, 4}) && scopes.panelRect(3) == scopes.cardRect(1), "Dropping outside the grid changed the order or left a floating panel.");
        scopes.findChild<QPushButton *>("showAllScopes")->click();
        require(visibility->model()->moveRows({}, 4, 1, {}, 0), "Channel list cannot move overflow entries.");
        require(scopes.visibleChannels() == QVector<int>({4, 1, 2, 3}), "Channel list reorder did not update the grid.");
        numbers->click(); require(!numbers->isChecked() && names->isChecked(), "Number toggle also changed names.");
        names->click(); require(!numbers->isChecked() && !names->isChecked(), "Name toggle failed.");
        scopes.grab().save(QDir(directory).filePath("native-editor-no-labels.png")); names->click();
        for (int i = 0; i < visibility->count(); ++i) visibility->item(i)->setCheckState(Qt::Unchecked);
        until(app, [&] { return window.player().state().scopesSuspended; }, "All-hidden grid still computes.");
        const auto emptyGrid = window.player().state(); pump(app, 250);
        require(scopes.visibleChannels().isEmpty() && window.player().state().scopeBlocks == emptyGrid.scopeBlocks && window.player().state().positionMs > emptyGrid.positionMs + 100, "Empty grid interrupted audio or kept rendering.");
        scopes.findChild<QPushButton *>("showAllScopes")->click(); keepGrid->setChecked(true);
        visibility->item(0)->setCheckState(Qt::Unchecked);
        window.loadFile(paths[2]);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.path == paths[2]; }, "Keep-grid file load failed.");
        window.refresh();
        require(rows->value() == 2 && columns->value() == 2 && scopes.visibleChannels() == QVector<int>({0, 1, 2, 3}) && visibility->count() == 9, "Keep grid also retained hidden/order or changed dimensions.");
        require(!numbers->isChecked() && names->isChecked(), "General label preferences reset with a file.");
        keepGrid->setChecked(false); window.loadFile(paths[0]);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.path == paths[0]; }, "Grid reset file load failed.");
        window.refresh(); require(rows->value() == 3 && columns->value() == 2, "New file did not fit enabled Full mix with Keep grid off.");
        rows->setValue(2); columns->setValue(2); visibility->item(1)->setCheckState(Qt::Unchecked);
        const auto subsongLayout = scopes.visibleChannels();
        window.findChild<QPushButton *>("next")->click();
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 1; }, "Layout subsong switch failed.");
        window.refresh(); require(scopes.visibleChannels() == subsongLayout && rows->value() == 2 && columns->value() == 2, "Subsong change reset layout.");
        keepGrid->setChecked(true); window.loadFile(paths[0]);
        until(app, [&] { return !window.player().state().busy && window.player().state().info.song == 0; }, "Same-file reopen failed.");
        window.refresh(); require(scopes.visibleChannels() == QVector<int>({0, 1, 2, 3}), "Explicit same-file open did not reset channel visibility.");
        scopes.resize(480, 360); pump(app, 50); scopes.grab().save(QDir(directory).filePath("native-editor-small.png"));
        auto apply = scopes.findChild<QPushButton *>("applyLayout"); auto reset = scopes.findChild<QPushButton *>("resetLayout");
        require(apply->isVisible() && apply->parentWidget() == reset->parentWidget() && apply->geometry().left() > reset->geometry().right(), "Apply is not next to Reset.");
        scopes.resize(1000, 680); pump(app, 50); scopes.grab().save(QDir(directory).filePath("native-scope-editor.png"));
        require(std::abs(rows->mapTo(&scopes, rows->rect().center()).y() - names->mapTo(&scopes, names->rect().center()).y()) < 5, "Wide editor did not put grid and label controls on one line.");
        names->setChecked(false); visibility->item(2)->setCheckState(Qt::Unchecked);
        scopes.findChild<QPushButton *>("resetLayout")->click();
        require(rows->value() == 5 && columns->value() == 1 && scopes.visibleChannels() == QVector<int>({0, 1, 2, 3, 4}) && numbers->isChecked() && names->isChecked(), "Reset did not restore the full default view.");
        apply->click(); require(!edit->isChecked() && !rows->isVisible() && !visibility->isVisible() && scopes.visibleChannels().size() == 5, "Apply did not finish editing and preserve the layout.");
        numbers->setChecked(true); keepGrid->setChecked(false);
        log << "Scope editor: grid/labels, animated pointer-following drag/drop/cancel, list reorder, responsive toolbar, Apply/Reset, visible-only render, empty grid, reset/preserve dimensions, subsong retention PASS\n"; log.flush();
        // Local user files are only opened, never copied/exported/modified.
        for (int i = option + 2; i < arguments.size(); ++i) {
            const auto path = arguments[i];
            if (path.startsWith('-')) continue;
            window.loadFile(path);
            until(app, [&] { return !window.player().state().busy && window.player().state().positionMs > 300; }, "Real file failed to start.");
            const auto before = window.player().state();
            QElapsedTimer elapsed; elapsed.start();
            const int duration = path.endsWith("vgz", Qt::CaseInsensitive) ? 12000 : 2500;
            while (elapsed.elapsed() < duration) {
                window.resize(400 + (elapsed.elapsed() / 100 % 4) * 20, 430);
                window.scopeWindow().resize(900 + (elapsed.elapsed() / 100 % 5) * 45, 680);
                // Intentionally stall GUI for longer than the PCM prebuffer.
                QThread::msleep(250);
                pump(app, 50);
            }
            auto after = window.player().state();
            const auto scope = window.player().scopes();
            log << "Real " << QFileInfo(path).suffix() << ": GUI stalls/resizing, " << elapsed.elapsed()
                << " ms; empty callbacks=" << after.starvations - before.starvations
                << ", output errors=" << after.outputErrors - before.outputErrors
                << ", scope lag=" << after.positionMs - scope.positionMs << " ms\n"; log.flush();
            require(after.playing && after.error.isEmpty() && after.scopeError.isEmpty(), "Real playback/scopes failed.");
            require(after.positionMs - before.positionMs >= duration - 500, "Audio clock stalled with GUI.");
            require(after.starvations == before.starvations && after.outputErrors == before.outputErrors, "Audio starvation under GUI load.");
            require(scope.generation == after.generation && after.positionMs - scope.positionMs < 500, "Scopes lag behind real playback.");
            if (path.endsWith("vgz", Qt::CaseInsensitive)) {
                edit->click(); rows->setValue(1); columns->setValue(2);
                const int latePosition = before.info.durationMs > 0 ? std::min(60000, before.info.durationMs / 2) : 60000;
                window.player().seek(latePosition, true);
                until(app, [&] { auto s = window.player().state(); auto f = window.player().scopes();
                    return !s.busy && f.generation == s.generation && f.mask == 3 && s.positionMs - f.positionMs < 200;
                }, "Late VGZ scopes failed to prepare.", 45000);
                const auto stable = window.player().scopes(); const auto stableState = window.player().state();
                QElapsedTimer catchup; catchup.start();
                visibility->item(0)->setCheckState(Qt::Unchecked); // Channel 2 stays live; channel 3 enters from overflow.
                require((window.player().scopes().mask & 2) && !window.player().scopes().channels[1].isEmpty(), "Mask edit cleared an unchanged VGZ scope.");
                until(app, [&] { auto f = window.player().scopes(); return (f.mask & 2) && f.positionMs > stable.positionMs + 50; }, "Unchanged VGZ scope froze after hiding another channel.", 500);
                int maximumLag = 0;
                while (!(window.player().scopes().mask & 4) && catchup.elapsed() < 30000) {
                    pump(app, 25);
                    auto s = window.player().state(); auto f = window.player().scopes();
                    require((f.mask & 2) && f.generation == s.generation, "Preparing a new VGZ scope blanked an existing scope.");
                    maximumLag = std::max(maximumLag, s.positionMs - f.positionMs);
                    require(s.positionMs - f.positionMs < 300, "New VGZ channel preparation blocked existing waves.");
                }
                auto prepared = window.player().scopes();
                require(prepared.mask == 6 && prepared.channels[0].isEmpty() && !prepared.channels[2].isEmpty(), "New VGZ channel did not catch up independently.");
                const auto preparedState = window.player().state();
                require(preparedState.starvations == stableState.starvations && preparedState.outputErrors == stableState.outputErrors && preparedState.playing, "Scope selection interrupted VGZ audio.");
                log << "VGZ selection at " << latePosition << " ms: unchanged channel kept updating; replacement prepared in " << catchup.elapsed()
                    << " ms; maximum live-scope lag=" << maximumLag << " ms; audio errors=0 PASS\n"; log.flush();
                reset->click(); apply->click();
                window.scopeWindow().hide(); auto hiddenBefore = window.player().state(); pump(app, 2000);
                auto hiddenAfter = window.player().state();
                require(hiddenAfter.starvations == hiddenBefore.starvations && hiddenAfter.scopesSuspended, "Hidden scopes playback or suspension failed.");
                window.scopeWindow().show();
            }
        }
        window.loadFile(namedPath); pump(app, 600); window.resize(430, 430); pump(app, 100);
        window.grab().save(QDir(directory).filePath("native-player.png"));
        window.findChild<QTabWidget *>("tabs")->setCurrentIndex(1); pump(app, 50);
        window.grab().save(QDir(directory).filePath("native-channels.png"));
        window.scopeWindow().grab().save(QDir(directory).filePath("native-scopes.png"));
        const auto finalState = window.player().state();
        log << "Total empty callbacks=" << finalState.starvations << ", output errors=" << finalState.outputErrors << '\n';
        window.scopeWindow().close();
        until(app, [&] { return window.player().state().scopesSuspended; }, "Final scope suspension failed.");
        window.findChild<QPushButton *>("windowClose")->click(); pump(app, 50);
        require(!window.player().isRunning() && !window.isVisible() && !window.scopeWindow().isVisible(), "Closing main window did not shut down a suspended worker.");
        { PlayerWindow closingVisible; closingVisible.player().setVolume(0); closingVisible.show(); closingVisible.loadFile(paths[0]);
          until(app, [&] { return !closingVisible.player().state().busy && closingVisible.player().state().playing; }, "Final shutdown fixture failed.");
          closingVisible.close(); require(!closingVisible.player().isRunning() && !closingVisible.scopeWindow().isVisible(), "Main close left visible scopes alive."); }
        checkShutdown(app, paths + seekPaths, log);
        log << "All native checks PASS. Qt exposes output errors; hardware underflow flags are not available here.\n";
        return 0;
    } catch (const std::exception &error) { log << "FAIL: " << error.what() << '\n'; return 1; }
}
