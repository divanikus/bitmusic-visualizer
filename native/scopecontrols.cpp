#include "window.h"
#include "appsettings.h"
#include "palette.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

void ScopeWindow::restoreWaves() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    settings.beginGroup("Waveforms");
    auto number = [&](const char *key, int fallback, int low, int high) {
        bool ok = false; const int value = settings.value(key, fallback).toInt(&ok);
        return ok ? std::clamp(value, low, high) : fallback;
    };
    const auto scale = settings.value("Scale", "smooth").toString();
    waveOptions_.scale = scale == "instant" ? WaveOptions::Instant : scale == "fixed" ? WaveOptions::Fixed : WaveOptions::Smooth;
    const auto trigger = settings.value("Trigger", "stable").toString();
    waveOptions_.trigger = trigger == "rising" ? WaveOptions::Rising : trigger == "off" ? WaveOptions::Off : WaveOptions::Stable;
    waveOptions_.holdMs = number("HoldMs", 300, 0, 9999);
    waveOptions_.releaseMs = number("ReleaseMs", 1000, 1, 9999);
    const auto output = settings.value("OutputMix", false).toString().trimmed().toLower();
    waveOptions_.output = output == "true" || output == "1";
}
void ScopeWindow::rememberWaves() {
    QSettings settings(playerSettingsPath(), playerSettingsFormat()); settings.setFallbacksEnabled(false);
    settings.beginGroup("Waveforms");
    const char *scales[] = {"smooth", "instant", "fixed"}, *triggers[] = {"stable", "rising", "off"};
    settings.setValue("Scale", scales[waveOptions_.scale]); settings.setValue("Trigger", triggers[waveOptions_.trigger]);
    settings.setValue("HoldMs", waveOptions_.holdMs); settings.setValue("ReleaseMs", waveOptions_.releaseMs);
    settings.setValue("OutputMix", waveOptions_.output); settings.sync();
    if (settings.status() != QSettings::NoError)
        QMessageBox::warning(this, "Could not save settings", "The waveform settings are applied for this session, but could not be saved to:\n" +
            playerSettingsPath() + "\n\nPlease check that this settings location is writable.");
}
void ScopeWindow::editWaves() {
    QDialog dialog(this); dialog.setWindowTitle("Waveforms"); dialog.setObjectName("scopeWavesDialog"); dialog.setMinimumWidth(420);
    auto layout = new QVBoxLayout(&dialog); auto form = new QFormLayout; layout->addLayout(form);
    auto scale = new QComboBox; scale->setObjectName("waveScale"); scale->addItems({"Smooth auto", "Instant auto", "Fixed (full scale)"});
    styleCombo(scale); scale->setCurrentIndex(waveOptions_.scale); form->addRow("Wave height", scale);
    auto scaleHelp = new QLabel; scaleHelp->setObjectName("waveScaleHelp"); scaleHelp->setWordWrap(true); scaleHelp->setMaximumWidth(290);
    form->addRow(QString(), scaleHelp);
    auto hold = new QSpinBox; hold->setObjectName("waveHold"); hold->setRange(0, 9999); hold->setSuffix(" ms"); hold->setValue(waveOptions_.holdMs);
    hold->setToolTip("How long to keep the scale after a loud note, so its fading sound looks smaller."); form->addRow("Hold", hold);
    auto release = new QSpinBox; release->setObjectName("waveRelease"); release->setRange(1, 9999); release->setSuffix(" ms"); release->setValue(waveOptions_.releaseMs);
    release->setToolTip("How slowly quiet waves grow on screen after Hold. Larger values mean slower changes."); form->addRow("Release", release);
    auto trigger = new QComboBox; trigger->setObjectName("waveTrigger"); trigger->addItems({"Stable", "Rising edge", "Off"});
    styleCombo(trigger); trigger->setCurrentIndex(waveOptions_.trigger); form->addRow("Trigger (alignment)", trigger);
    auto triggerHelp = new QLabel; triggerHelp->setObjectName("waveTriggerHelp"); triggerHelp->setWordWrap(true); triggerHelp->setMaximumWidth(290);
    form->addRow(QString(), triggerHelp);
    auto explain = [&] {
        const QString scales[] = {
            "Fits the wave to the card. Hold keeps the scale after a loud note; Release controls how slowly quiet waves grow again.",
            "Fits every frame immediately. Quiet and loud sounds can appear equally tall.",
            "Keeps the same scale. Quiet sounds stay smaller, making volume changes easier to compare."};
        const QString triggers[] = {
            "Finds a similar rising start in each frame to reduce sideways jumping. Noise and complex music may still move.",
            "Starts at the first upward crossing of the center line. Simple periodic waves usually stay steady.",
            "Does not align the wave. It moves as the audio time window advances."};
        scaleHelp->setText(scales[scale->currentIndex()]); triggerHelp->setText(triggers[trigger->currentIndex()]);
    };
    auto note = new QLabel("Display only: these settings do not change the sound.\nChanges are remembered automatically.");
    note->setObjectName("editorHint"); layout->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close | QDialogButtonBox::RestoreDefaults); layout->addWidget(buttons);
    bool edited = false;
    auto changed = [&] {
        edited = true;
        waveOptions_ = {WaveOptions::Scale(scale->currentIndex()), WaveOptions::Trigger(trigger->currentIndex()), hold->value(), release->value(), waveOptions_.output};
        hold->setEnabled(waveOptions_.scale == WaveOptions::Smooth); release->setEnabled(hold->isEnabled());
        waveStates_ = {}; if (gpu_) gpu_->suspend();
        explain();
        update();
    };
    connect(scale, &QComboBox::currentIndexChanged, &dialog, changed); connect(trigger, &QComboBox::currentIndexChanged, &dialog, changed);
    connect(hold, &QSpinBox::valueChanged, &dialog, changed); connect(release, &QSpinBox::valueChanged, &dialog, changed);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] {
        const WaveOptions defaults;
        scale->setCurrentIndex(defaults.scale); trigger->setCurrentIndex(defaults.trigger);
        hold->setValue(defaults.holdMs); release->setValue(defaults.releaseMs);
    });
    hold->setEnabled(waveOptions_.scale == WaveOptions::Smooth); release->setEnabled(hold->isEnabled());
    explain(); dialog.exec(); if (edited) rememberWaves();
}
