#include "scopewindow.h"
#include "appsettings.h"
#include "palette.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
class PresetCombo : public QComboBox {
public:
    std::function<void()> refresh;
    void showPopup() override { if (refresh) refresh(); QComboBox::showPopup(); }
};
QString presetPath(const QString &file) {
    if (file.isEmpty() || QFileInfo(file).fileName() != file || file.contains('\\') || !file.endsWith(".ini", Qt::CaseInsensitive)) return {};
    return QDir(playerPresetsDirectory()).filePath(file);
}
QString defaultKey(const QString &id) { return "PresetDefaults/" + id; }
}

void ScopeWindow::setupPresets(QVBoxLayout *layout) {
    layout->addWidget(new QLabel("Preset"));
    auto combo = new PresetCombo; presets_ = combo; combo->setObjectName("scopePreset"); styleCombo(combo);
    combo->setAccessibleName("Layout preset"); combo->setMinimumWidth(0);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); combo->setMinimumContentsLength(7);
    combo->addItem("Current layout", QString()); combo->setEnabled(false); layout->addWidget(combo);
    presetDefault_ = new QPushButton("Use by default"); presetDefault_->setCheckable(true); presetDefault_->setObjectName("presetDefault");
    presetSave_ = new QPushButton("Save preset..."); presetSave_->setObjectName("savePreset");
    presetSave_->setToolTip("Save layout preset..."); presetSave_->setAccessibleName(presetSave_->toolTip());
    presetDefault_->setEnabled(false); presetSave_->setEnabled(false);
    layout->addWidget(presetSave_); layout->addWidget(presetDefault_);
    presetStatus_ = new QLabel; presetStatus_->setObjectName("presetStatus"); presetStatus_->setWordWrap(true); layout->addWidget(presetStatus_);
    combo->refresh = [this] { refreshPresets(); };
    connect(combo, &QComboBox::activated, this, [this] {
        const auto file = presets_->currentData().toString();
        if (file.isEmpty()) { refreshPresets(); return; }
        ScopePreset candidate; QString error;
        if (!candidate.load(presetPath(file), error) || !candidate.compatible(state_.info.configuration)) {
            refreshPresets(); QMessageBox::warning(this, "Could not load preset", error.isEmpty() ? "This preset is not compatible with the current channels." : error); return;
        }
        if (candidate.configuration.id == state_.info.configuration.id) applyPreset(candidate, file);
        else {
            const auto adapted = candidate.adapted(state_.info.configuration);
            if (adapted.cards.size() > ScopeCards::Maximum) {
                refreshPresets(); QMessageBox::warning(this, "Too many cards", "This transfer would exceed 512 cards. Remove some duplicate cards in the original preset first."); return;
            }
            suggestedPresetName_ = QFileInfo(file).completeBaseName() + " - " + state_.info.configuration.name;
            suggestedPresetName_.replace('/', '-');
            applyPreset(adapted, {});
            presets_->setItemText(0, "Adapted layout");
            presets_->setToolTip("Adapted from " + QFileInfo(file).completeBaseName() +
                ". Shared hardware channels keep their cards; new channels use waveforms. Save to keep this version.");
        }
    });
    connect(presetSave_, &QPushButton::clicked, this, [this] { savePreset(); });
    connect(presetDefault_, &QPushButton::clicked, this, [this](bool checked) {
        const auto id = state_.info.configuration.id;
        if (id.isEmpty()) return;
        if (checked) {
            ScopePreset preset; QString error;
            if (!preset.load(presetPath(presetFile_), error) || preset.configuration.id != id || !preset.compatible(state_.info.configuration)) {
                refreshPresets(); return;
            }
        }
        QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
        if (checked) settings.setValue(defaultKey(id), presetFile_); else settings.remove(defaultKey(id));
        settings.sync();
        if (settings.status() != QSettings::NoError)
            QMessageBox::warning(this, "Could not save preset default", "Please check that the settings location is writable:\n" + playerSettingsPath());
        refreshPresets();
    });
}
ScopePreset ScopeWindow::currentPreset() const {
    ScopePreset preset; preset.configuration = state_.info.configuration;
    preset.rows = rows_->value(); preset.columns = columns_->value();
    preset.numbers = numbers_->isChecked(); preset.names = names_->isChecked(); preset.stereo = stereo_->isChecked();
    preset.cards = cards_.presetCards(preset.configuration);
    return preset;
}
void ScopeWindow::applyPreset(const ScopePreset &preset, const QString &file) {
    clearCardSelection();
    applyingPreset_ = true; automaticGrid_ = false;
    ++cardRevision_;
    if (appearanceDialog_) appearanceDialog_->reject();
    cancelDrag(); if (gpu_) gpu_->suspend();
    const QSignalBlocker r(rows_), c(columns_), n(numbers_), a(names_), s(stereo_);
    rows_->setValue(preset.rows); columns_->setValue(preset.columns);
    numbers_->setChecked(preset.numbers); names_->setChecked(preset.names); stereo_->setChecked(preset.stereo);
    cards_.loadPreset(preset, {bool(state_.info.noteRoll), bool(state_.info.patterns)}, waveOptions_.meters);
    analysis_.clear();
    presetFile_ = file; savedPreset_ = file.isEmpty() ? QByteArray() : currentPreset().encode(); presetModified_ = false;
    rebuildList(); layoutChanged(); updateViewEditor();
    applyingPreset_ = false; refreshPresets();
}
bool ScopeWindow::restoreDefaultPreset() {
    suggestedPresetName_.clear(); defaultPresetFile_.clear();
    const auto &config = state_.info.configuration;
    if (config.id.isEmpty()) return false;
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    defaultPresetFile_ = settings.value(defaultKey(config.id)).toString();
    const auto path = presetPath(defaultPresetFile_);
    if (path.isEmpty()) return false;
    ScopePreset preset; QString error;
    if (!preset.load(path, error) || preset.configuration.id != config.id || !preset.compatible(config)) return false;
    applyPreset(preset, defaultPresetFile_); return true;
}
void ScopeWindow::refreshPresets() {
    if (!presets_) return;
    QSignalBlocker block(presets_);
    presets_->clear(); presets_->addItem("Current layout", QString());
    const auto &config = state_.info.configuration;
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    defaultPresetFile_ = config.id.isEmpty() ? QString() : settings.value(defaultKey(config.id)).toString();
    struct Entry { QString file, name, configuration; bool exact; };
    QVector<Entry> entries;
    for (const auto &file : QDir(playerPresetsDirectory()).entryInfoList({"*.ini"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        ScopePreset preset; QString error;
        if (preset.load(file.absoluteFilePath(), error) && preset.compatible(config))
            entries.push_back({file.fileName(), file.completeBaseName(), preset.configuration.name, preset.configuration.id == config.id});
    }
    for (bool exact : {true, false}) {
        bool heading = false;
        for (const auto &entry : entries) if (entry.exact == exact) {
            if (!exact && !heading) { presets_->insertSeparator(presets_->count()); heading = true; }
            presets_->addItem(entry.name + (exact ? "" : " (adapt)"), entry.file);
            presets_->setItemData(presets_->count()-1, entry.configuration, Qt::ToolTipRole);
        }
    }
    int selected = presets_->findData(presetFile_);
    if (selected < 0) { presetFile_.clear(); savedPreset_.clear(); presetModified_ = false; selected = 0; }
    presets_->setCurrentIndex(selected); updatePresetStatus();
}
void ScopeWindow::updatePresetStatus() {
    if (!presets_) return;
    const bool ready = state_.valid && !newFile_ && !state_.info.configuration.id.isEmpty();
    presets_->setEnabled(ready); presetSave_->setEnabled(ready);
    const int selected = presets_->findData(presetFile_);
    if (selected > 0) presets_->setItemText(selected, QFileInfo(presetFile_).completeBaseName() + (presetModified_ ? " *" : ""));
    const bool checked = !defaultPresetFile_.isEmpty() && (presetFile_.isEmpty() || defaultPresetFile_ == presetFile_);
    presetDefault_->setChecked(checked); presetDefault_->setEnabled(ready && (checked || !presetFile_.isEmpty()));
    const auto group = state_.info.configuration.name;
    presetStatus_->setText(defaultPresetFile_.isEmpty() ? "Default: None" : "Default: " + QFileInfo(defaultPresetFile_).completeBaseName());
    presetStatus_->setToolTip(group);
    presetDefault_->setText(checked ? "Clear default" : "Use by default");
    presetDefault_->setToolTip(checked ? "Clear default preset for " + group + " (" + QFileInfo(defaultPresetFile_).completeBaseName() + ")"
        : "Use this saved preset automatically for " + group + ". Unsaved edits are not included.");
    presetDefault_->setAccessibleName(presetDefault_->toolTip());
    presets_->setToolTip(group + " layouts. Selection applies immediately; * means unsaved edits.\n(adapt) copies shared hardware channels into this configuration.");
}
void ScopeWindow::presetEdited() {
    if (applyingPreset_ || !presets_) return;
    if (!presetFile_.isEmpty()) presetModified_ = currentPreset().encode() != savedPreset_;
    updatePresetStatus();
}
void ScopeWindow::savePreset() {
    if (!state_.valid || state_.info.configuration.id.isEmpty()) return;
    const auto config = state_.info.configuration.id;
    QDialog dialog(this); dialog.setWindowTitle("Save layout preset"); dialog.setObjectName("savePresetDialog"); dialog.setMinimumWidth(340);
    dialog.setPalette(playerPalette()); auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Preset name"));
    auto name = new QLineEdit; name->setObjectName("presetName"); name->setMaxLength(80);
    name->setText(!presetFile_.isEmpty() ? QFileInfo(presetFile_).completeBaseName() :
        !suggestedPresetName_.isEmpty() ? suggestedPresetName_.left(80) : QString(state_.info.configuration.name).replace('/', '-') + " layout");
    layout->addWidget(name);
    auto hint = new QLabel; hint->setObjectName("presetNameHint"); hint->setWordWrap(true); layout->addWidget(hint);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    auto save = buttons->button(QDialogButtonBox::Save); QString file;
    auto validate = [&] {
        auto title = name->text().trimmed(); if (title.endsWith(".ini", Qt::CaseInsensitive)) title.chop(4);
        const bool valid = !title.isEmpty() && !title.endsWith('.') && !title.endsWith(' ') &&
            !title.contains(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])")) &&
            !QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\\.|$)", QRegularExpression::CaseInsensitiveOption).match(title).hasMatch();
        file = valid ? title + ".ini" : QString();
        bool replace = valid && QFileInfo::exists(presetPath(file));
        // A name shared by another configuration would break its default binding.
        ScopePreset existing; QString error;
        const bool foreign = replace && (!existing.load(presetPath(file), error) || existing.configuration.id != config);
        save->setEnabled(valid && !foreign); save->setText(replace ? "Replace" : "Save");
        hint->setText(foreign ? "This name belongs to another configuration or an unreadable preset. Choose a new name." :
            !valid ? "Please choose a different preset name." : replace ? "Replace the saved layout with this name." : QString());
    };
    connect(name, &QLineEdit::textChanged, &dialog, validate);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        validate(); if (!save->isEnabled()) return;
        if (state_.info.configuration.id != config || newFile_) { dialog.reject(); return; }
        const auto preset = currentPreset(); QString error;
        if (!QDir().mkpath(playerPresetsDirectory())) error = "Could not create the presets folder.";
        else if (preset.save(presetPath(file), error)) {
            presetFile_ = file; savedPreset_ = preset.encode(); presetModified_ = false; suggestedPresetName_.clear();
            refreshPresets(); dialog.accept(); return;
        }
        hint->setText("Could not save preset: " + error);
    });
    validate(); name->selectAll(); name->setFocus(); dialog.exec();
}
