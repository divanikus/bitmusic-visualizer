#include "midicontrols.h"
#include "miditrack.h"
#include "mt32track.h"
#include "playercontrols.h"
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

MidiControls::MidiControls(QWidget *parent) : QWidget(parent) {
    setObjectName("midiControls");
    auto row = new QHBoxLayout(this); row->setContentsMargins(0, 0, 0, 0);
    engine_ = new QComboBox; engine_->setObjectName("midiEngine");
    engine_->addItem("Auto", "auto"); engine_->addItem("General MIDI", "gm"); engine_->addItem("MT-32", "mt32");
    engine_->setCurrentIndex(engine_->findData(selectedMidiEngine()));
    engine_->setToolTip("MIDI engine. Auto recognizes MT-32 SysEx; select MT-32 manually for arrangements without it. Changing the engine restarts playback.");
    row->addWidget(engine_);
    soundFont_ = new QComboBox; soundFont_->setObjectName("soundFont");
    soundFont_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    soundFont_->setMinimumContentsLength(12);
    soundFont_->setToolTip("General MIDI bank or SFLIST set. Changing it restarts the current MIDI.");
    row->addWidget(soundFont_, 1);
    folder_ = createPlayerIconButton(PlayerIcon::Open, "soundFontFolder", "Open SoundFonts folder"); row->addWidget(folder_);
    refresh_ = new QToolButton; refresh_->setText(QString::fromUtf8("↻"));
    refresh_->setObjectName("refreshSoundFonts"); refresh_->setToolTip("Refresh SoundFont list"); row->addWidget(refresh_);
    hide();
    connect(engine_, &QComboBox::activated, this, [this](int index) {
        if (index < 0 || engine_->itemData(index).toString() == selectedMidiEngine()) return;
        try {
            selectMidiEngine(engine_->itemData(index).toString());
            if (!midiPath_.isEmpty() && onReloadRequested) onReloadRequested(midiPath_);
        } catch (const std::exception &e) { QMessageBox::warning(window(), "MIDI engine", QString::fromUtf8(e.what())); }
    });
    connect(soundFont_, &QComboBox::activated, this, [this](int index) {
        const auto name = soundFont_->itemData(index).toString();
        if (name.isEmpty() || name == selectedSoundFont()) return;
        try {
            selectSoundFont(name);
            if (!midiPath_.isEmpty() && onReloadRequested) onReloadRequested(midiPath_);
        } catch (const std::exception &e) { QMessageBox::warning(window(), "SoundFont", QString::fromUtf8(e.what())); }
    });
    connect(refresh_, &QToolButton::clicked, this, &MidiControls::refreshSoundFonts);
    connect(folder_, &QPushButton::clicked, this, [this] { if (onFolderRequested) onFolderRequested(); });
}
void MidiControls::prepareSource(const QString &path) {
    QFile input(path);
    const bool midi = input.open(QIODevice::ReadOnly) && input.read(4) == "MThd";
    midiPath_ = midi ? path : QString();
    setVisible(midi);
    if (midi) refreshSoundFonts();
}
void MidiControls::clearSource() { midiPath_.clear(); hide(); }
void MidiControls::refreshSoundFonts() {
    const QSignalBlocker blocked(soundFont_); soundFont_->clear();
    const auto selected = selectedSoundFont();
    for (const auto &name : availableSoundFonts()) soundFont_->addItem(QFileInfo(name).completeBaseName() + (name.endsWith(".sflist", Qt::CaseInsensitive) ? " (set)" : ""), name);
    int at = soundFont_->findData(selected);
    if (at < 0) { soundFont_->insertItem(0, selected.isEmpty() ? "No SoundFonts found" : "Missing: " + selected, QString()); at = 0; }
    soundFont_->setCurrentIndex(at);
}
bool MidiControls::usingMt32(const PlayerState &state) const {
    return engine_->currentData() == "mt32" || (engine_->currentData() == "auto" &&
        ((state.valid && state.info.mt32Resources) || state.error.contains("MT-32")));
}
void MidiControls::present(const PlayerState &state) {
    const bool mt32 = usingMt32(state);
    soundFont_->setVisible(!mt32); refresh_->setVisible(!mt32);
    folder_->setToolTip(mt32 ? "Open MT-32 ROM folder" : "Open SoundFonts folder");
}
QString MidiControls::resourceDirectory(const PlayerState &state) const {
    return usingMt32(state) ? playerMt32Directory() : playerSoundFontsDirectory();
}
