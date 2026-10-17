#include "playerwindow.h"
#include "appsettings.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <stdexcept>

void checkSoundConfigurations(QApplication &app, const QString &directory, QTextStream &log);

namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pump(QApplication &app, int ms = 40) { QEventLoop loop(&app); QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
void until(QApplication &app, const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 8000) pump(app);
    require(ready(), "Live preset playback/capture did not become ready.");
}
QByteArray read(const QString &path) { QFile f(path); require(f.open(QIODevice::ReadOnly), "Cannot read preset fixture."); return f.readAll(); }
void write(const QString &path, const QByteArray &data) { QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(data)==data.size(), "Cannot write preset fixture."); }
void select(QComboBox *combo, const QString &file) {
    const int index = combo->findData(file); require(index >= 0, "Preset missing from selector.");
    combo->setCurrentIndex(index); combo->activated(index);
}
void save(ScopeWindow &scope, const QString &name) {
    bool saved = false;
    QTimer::singleShot(0, &scope, [&] {
        auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        dialog->findChild<QLineEdit *>("presetName")->setText(name);
        auto button = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
        if (!button->isEnabled()) { dialog->reject(); return; }
        button->click(); saved = dialog->result() == QDialog::Accepted;
    });
    scope.findChild<QPushButton *>("savePreset")->click(); require(saved, "Preset name dialog did not save.");
}
}

void checkPresets(QApplication &app, const QString &directory, QTextStream &log) {
    checkSoundConfigurations(app, directory, log);
    // A private root within the self-test root also isolates this suite from other checks.
    QTemporaryDir storage(QDir(directory).absoluteFilePath("preset-checks-XXXXXX")); require(storage.isValid(), "Cannot create preset test root.");
    const auto oldThemes = app.property("themesDirectoryForTests"), oldSettings = app.property("settingsFileForTests");
    struct Restore {
        QApplication &app; QVariant themes, settings;
        ~Restore() { app.setProperty("themesDirectoryForTests", themes); app.setProperty("settingsFileForTests", settings); }
    } restore{app, oldThemes, oldSettings};
    app.setProperty("themesDirectoryForTests", storage.filePath("themes"));
    app.setProperty("settingsFileForTests", storage.filePath("settings.ini"));
    const QDir fixtures(directory); auto nes = GmeTrack(fixtures.filePath("demo.nsf")).info();
    auto nsfe = GmeTrack(fixtures.filePath("named.nsfe")).info();
    require(nes.configuration.id == nsfe.configuration.id, "NSF/NSFE with identical chips use different preset keys.");
    auto expansion = read(nes.path); expansion[0x7b] = 1;
    write(storage.filePath("vrc6.nsf"), expansion); auto vrc6 = GmeTrack(storage.filePath("vrc6.nsf")).info();
    expansion[0x7b] = 8|32; write(storage.filePath("mmc5-5b.nsf"), expansion);
    auto both = GmeTrack(storage.filePath("mmc5-5b.nsf")).info();
    require(vrc6.voices.size() == 8 && vrc6.configuration.id != nes.configuration.id &&
        vrc6.configuration.sources.mid(0,5) == nes.configuration.sources &&
        both.configuration.sources[5] == "nes.s5b:0" && both.configuration.sources[8] == "nes.mmc5:0",
        "NES source identity confused expansion order or duplicate Square names.");
    auto vgm = GmeTrack(fixtures.filePath("demo.vgm")).info(), vgz = GmeTrack(fixtures.filePath("demo.vgz")).info();
    require(vgm.configuration.id == vgz.configuration.id, "VGM/VGZ compression affects preset identity.");
    auto changedMetadata = read(nes.path); changedMetadata.replace(14, 5, "Other");
    require(soundConfiguration(changedMetadata, nes.voices).id == nes.configuration.id, "Song metadata affects preset identity.");

    ScopePreset preset; preset.configuration = nes.configuration; preset.rows = 3; preset.columns = 3;
    preset.numbers = false; preset.stereo = true;
    preset.cards = {{"nes.apu:2", 2, {2,1}}, {"nes.apu:0", 1, {1,2}, true, true, false},
        {"nes.apu:0", 0, {1,1}, false}, {"mix", 1, {3,1}}, {"nes.apu:4", 0, {1,1}, false}};
    for (auto &card : preset.cards) card.levels = false;
    preset.cards[0].levels = true;
    const auto presetsDir = playerPresetsDirectory(); require(QDir().mkpath(presetsDir), "Cannot create presets directory.");
    const auto baseFile = QDir(presetsDir).filePath("NES studio.ini"); QString error;
    require(preset.save(baseFile, error), "Preset write failed.");
    ScopePreset loaded; require(loaded.load(baseFile, error) && loaded.encode() == preset.encode(), "Preset INI roundtrip lost layout state.");
    auto imported = loaded.adapted(vrc6.configuration);
    for (auto &card : imported.cards) if (!card.levels) card.levels = false;
    require(imported.cards.size() == preset.cards.size()+3 && imported.cards[1].source == "nes.apu:0" &&
        imported.cards[5].source == "nes.vrc6:0" && imported.cards[5].view == 0 && imported.cards[5].visible,
        "Adaptation lost shared cards or failed to append new hardware.");
    require(imported.adapted(nes.configuration).cards.size() == preset.cards.size(), "Reverse adaptation retained nonexistent expansion cards.");
    auto spc = GmeTrack(fixtures.filePath("demo.spc")).info();
    require(!preset.compatible(spc.configuration), "Unrelated chip platforms match.");
    const auto untouched = loaded.encode();
    for (const auto &bad : {QByteArray("[Preset]\nVersion=99\n"), preset.encode().replace("Source=nes.apu:0", "Source=unrelated:0"),
            preset.encode().replace("Rows=3", "Rows=999"), preset.encode().replace("Cards=5", "Cards=999999"),
            preset.encode().replace("View=2", "View=-1")}) {
        write(storage.filePath("bad.ini"), bad);
        require(!loaded.load(storage.filePath("bad.ini"), error) && loaded.encode() == untouched, "Invalid preset partly applied.");
    }
    log << "Presets: stable hardware identities, NSF/NSFE and VGM/VGZ matching, expansion transfer, INI roundtrip and validation PASS\n"; log.flush();

    ScopeWindow scope; scope.resize(1080, 720); scope.show(); PlayerState state; state.valid = true; state.info = nes;
    int masks = 0; scope.onChannelsChanged = [&](VoiceMask) { ++masks; }; scope.present(state, {});
    scope.findChild<QPushButton *>("editLayout")->click(); pump(app);
    auto combo = scope.findChild<QComboBox *>("scopePreset");
    auto star = scope.findChild<QPushButton *>("presetDefault");
    auto rows = scope.findChild<QSpinBox *>("gridRows"), columns = scope.findChild<QSpinBox *>("gridColumns");
    auto list = scope.findChild<QListWidget *>("scopeChannelList");
    auto view = scope.findChild<QComboBox *>("cardView");
    auto keep = scope.findChild<QCheckBox *>("keepGrid");
    require(combo && !star->isChecked() && rows->value() == 5 && columns->value() == 1, "Unseen configuration changed initial behavior.");
    masks = 0; select(combo, "NES studio.ini");
    require(masks == 1 && list->count() == 5 && rows->value() == 3 && columns->value() == 3 &&
        !scope.findChild<QCheckBox *>("channelNumbers")->isChecked() && scope.findChild<QCheckBox *>("scopeStereo")->isChecked(),
        "Preset apply did not restore grid/labels/stereo/cards atomically.");
    require(scope.visibleChannels() == QVector<int>({2,0,ScopeTheme::FullMix}) && list->item(0)->data(Qt::UserRole).toInt() == 2,
        "Preset lost order, hidden duplicate or Full mix placement.");
    list->setCurrentRow(1);
    require(view->currentIndex() == 1 && scope.findChild<QComboBox *>("spectrumStyle")->currentIndex() == 1 &&
        !scope.findChild<QCheckBox *>("spectrumGrid")->isChecked(), "Preset lost spectrum options.");
    save(scope, "Roundtrip"); ScopePreset saved;
    require(saved.load(QDir(presetsDir).filePath("Roundtrip.ini"), error) && saved.encode() == preset.encode(), "UI save changed card state.");
    star->click(); require(star->isChecked(), "Default assignment did not stick.");
    rows->setValue(4); require(combo->currentText().endsWith(" *"), "Dirty preset not marked.");
    state.info.song = 1; scope.present(state, {}); require(rows->value() == 4, "Subsong reloaded default and lost edits.");
    scope.beginFile(); state.info = nsfe; scope.present(state, {});
    require(rows->value() == 3 && combo->currentData() == "Roundtrip.ini", "Default not restored for compatible file.");
    require(read(QDir(presetsDir).filePath("Roundtrip.ini")) == preset.encode(), "Edits overwrote a saved preset automatically.");
    star->click(); require(!star->isChecked(), "Default could not be cleared.");
    keep->setChecked(true); rows->setValue(6); columns->setValue(2);
    scope.beginFile(); state.info = nes; scope.present(state, {});
    require(rows->value() == 6 && columns->value() == 2 && list->count() == 6 && !star->isChecked(), "Cleared default did not restore Keep grid behavior.");
    select(combo, "NES studio.ini"); star->click();
    scope.beginFile(); state.info = vrc6; scope.present(state, {});
    require(combo->currentData().toString().isEmpty() && !star->isChecked() && list->count() == 9,
        "A different configuration automatically inherited the base NES default.");
    select(combo, "NES studio.ini"); require(list->count() == 8 && combo->currentData().toString().isEmpty(), "Cross-configuration selection did not adapt.");
    save(scope, "VRC6 studio"); require(saved.load(QDir(presetsDir).filePath("VRC6 studio.ini"), error) && saved.encode() == imported.encode(), "Adapted UI snapshot lost cards/spans/sources.");
    star->click(); scope.grab().save(QDir(directory).filePath("native-presets-editor.png"));
    scope.resize(520, 400); pump(app); scope.grab().save(QDir(directory).filePath("native-presets-compact.png"));
    scope.close();
    {
        ScopeWindow reopened; reopened.present(state, {});
        require(reopened.findChild<QComboBox *>("scopePreset")->currentData() == "VRC6 studio.ini" &&
            reopened.findChild<QPushButton *>("presetDefault")->isChecked(), "Default was not restored in a new window.");
        reopened.findChild<QPushButton *>("resetLayout")->click();
        require(reopened.findChild<QPushButton *>("presetDefault")->isChecked(), "Reset removed a default binding.");
        reopened.findChild<QPushButton *>("presetDefault")->click();
        require(!reopened.findChild<QPushButton *>("presetDefault")->isChecked(), "Default could not be cleared after Reset.");
        reopened.close();
    }
    // Corrupt/missing default: do not load a related preset, and retain old layout initialization.
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    settings.setValue("PresetDefaults/" + nes.configuration.id, "missing.ini"); settings.sync();
    {
        ScopeWindow fallback; state.info = nes; fallback.present(state, {});
        require(fallback.findChild<QComboBox *>("scopePreset")->currentData().toString().isEmpty() &&
            fallback.findChild<QListWidget *>("scopeChannelList")->count() == 6, "Missing default did not fall back.");
        fallback.findChild<QPushButton *>("presetDefault")->click();
        require(!fallback.findChild<QPushButton *>("presetDefault")->isChecked(), "Missing default cannot be cleared.");
    }
    log << "Presets UI: compact controls, source/grid/view roundtrip, defaults/clear/restart, subsongs, Keep grid, NES-to-VRC6 transfer, atomic masks and missing fallback PASS\n"; log.flush();

    // A bad default must not fall through to a related preset or change channel IDs.
    write(QDir(presetsDir).filePath("corrupt.ini"), "[Preset]\nVersion=2\n");
    for (const QString &file : {QString("corrupt.ini"), QString("VRC6 studio.ini"), QString("../bad.ini")}) {
        settings.setValue("PresetDefaults/"+nes.configuration.id, file); settings.sync();
        ScopeWindow fallback; state.info = nes; fallback.present(state, {});
        require(fallback.findChild<QComboBox *>("scopePreset")->currentData().toString().isEmpty(), "Invalid default was applied.");
        require(fallback.findChild<QComboBox *>("scopePreset")->findData("VRC6 studio.ini") >= 0, "Same-platform preset not offered for manual transfer.");
    }
    {
        PlayerWindow playerWindow; playerWindow.player().setVolume(0); playerWindow.show();
        for (const auto &info : {nes, vgz}) {
            ScopePreset live; live.configuration = info.configuration; live.rows = 4; live.columns = 3; live.stereo = true;
            live.cards = {{live.configuration.sources[0], 1, {2,1}, true, true, false},
                {live.configuration.sources[0], 2, {2,1}}, {"mix", 0, {3,1}}};
            const auto file = info.configuration.family + " live.ini";
            require(live.save(QDir(presetsDir).filePath(file), error), "Cannot save live test preset.");
            settings.setValue("PresetDefaults/"+info.configuration.id, file); settings.sync();
            playerWindow.loadFile(info.path);
            auto &player = playerWindow.player(); auto &scopes = playerWindow.scopeWindow();
            until(app, [&] { return player.state().valid && !player.state().busy && player.state().positionMs > 200; });
            auto choices = scopes.findChild<QComboBox *>("scopePreset");
            until(app, [&] { return choices->currentData() == file && (player.scopes().mask & 1); });
            player.setMuteMask(2); player.setOutputMask(1);
            const auto before = player.state();
            scopes.findChild<QSpinBox *>("gridRows")->setValue(5); select(choices, file);
            until(app, [&] { return player.state().positionMs > before.positionMs+150 &&
                player.scopes().positionMs > before.positionMs && player.outputScopes().sequence > 0; });
            const auto after = player.state();
            require(after.generation == before.generation && after.muteMask == 2 && after.outputMask == 1 && after.playing &&
                !after.starvations && !after.outputErrors, "Applying a preset restarted or disrupted audio.");
            if (!scopes.findChild<QPushButton *>("editLayout")->isChecked()) scopes.findChild<QPushButton *>("editLayout")->click();
            scopes.resize(1080, 720); pump(app, 150);
            scopes.grab().save(QDir(directory).filePath("native-presets-live-"+info.configuration.family+".png"));
        }
        playerWindow.close();
    }
    log << "Presets: corrupt/incompatible/path-invalid defaults, same-platform list and live NSF/VGZ apply with unchanged transport/mutes and zero audio errors PASS\n"; log.flush();
}
