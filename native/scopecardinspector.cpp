#include "scopecardinspector.h"
#include "palette.h"
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

ScopeCardInspector::ScopeCardInspector(QWidget *parent) : QScrollArea(parent) {
    setObjectName("cardInspector");
    setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto content = new QWidget; setWidget(content);
    auto layout = new QVBoxLayout(content); layout->setContentsMargins(0, 4, 4, 0); layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    selectedCard_ = new QLabel("Select a card"); selectedCard_->setObjectName("selectedCard");
    selectedCard_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); layout->addWidget(selectedCard_);
    auto viewRow = new QHBoxLayout; viewRow->addWidget(new QLabel("View"));
    cardView_ = new QComboBox; cardView_->setObjectName("cardView"); cardView_->setAccessibleName("Card view"); styleCombo(cardView_);
    for (auto kind : {ScopeView::Waveform, ScopeView::Spectrum, ScopeView::Keyboard}) cardView_->addItem(scopeViewName(kind), int(kind));
    viewRow->addWidget(cardView_, 1); layout->addLayout(viewRow);
    spectrumControls_ = new QWidget;
    auto spectrumRow = new QHBoxLayout(spectrumControls_); spectrumRow->setContentsMargins(0, 0, 0, 0);
    spectrumRow->addWidget(new QLabel("Style"));
    spectrumStyle_ = new QComboBox; spectrumStyle_->setObjectName("spectrumStyle"); styleCombo(spectrumStyle_);
    spectrumStyle_->setAccessibleName("Spectrum style"); spectrumStyle_->addItems({"Line", "Bars"});
    spectrumRow->addWidget(spectrumStyle_, 1);
    spectrumGrid_ = new QCheckBox("Grid"); spectrumGrid_->setObjectName("spectrumGrid");
    spectrumGrid_->setToolTip("Show frequency and level grid lines and labels on this card");
    spectrumRow->addWidget(spectrumGrid_); layout->addWidget(spectrumControls_); spectrumControls_->hide();
    keyboardHint_ = new QLabel; keyboardHint_->setObjectName("keyboardHint"); keyboardHint_->setWordWrap(true);
    keyboardHint_->hide();
    viewHelp_ = new QPushButton("About this view"); viewHelp_->setObjectName("viewHelp"); viewHelp_->setCheckable(true);
    viewHelp_->hide();
    connect(viewHelp_, &QPushButton::toggled, this, [this](bool expanded) { keyboardHint_->setVisible(expanded); });
    musicControls_ = new QWidget;
    auto musicRow = new QVBoxLayout(musicControls_); musicRow->setContentsMargins(0, 0, 0, 0);
    rollDirection_ = new QComboBox; rollDirection_->setObjectName("rollDirection"); styleCombo(rollDirection_);
    rollDirection_->addItems({"Notes fall down", "Notes move left"}); musicRow->addWidget(rollDirection_);
    rollKeyboardSize_ = new QSpinBox; rollKeyboardSize_->setObjectName("rollKeyboardSize");
    rollKeyboardSize_->setPrefix("Keyboard size: "); rollKeyboardSize_->setSuffix(" %");
    rollKeyboardSize_->setRange(5, 80); rollKeyboardSize_->setSingleStep(5);
    rollKeyboardSize_->setToolTip("Keyboard height as a percentage of the plot. In horizontal view this controls its width. The note range stays unchanged.");
    musicRow->addWidget(rollKeyboardSize_);
    patternCommands_ = new QCheckBox("Volume and effects"); patternCommands_->setObjectName("patternCommands"); musicRow->addWidget(patternCommands_);
    patternChannel_ = new QSpinBox; patternChannel_->setObjectName("patternFirstChannel"); patternChannel_->setPrefix("First channel: ");
    patternChannel_->setToolTip("First column when the whole pattern does not fit. Enlarge the card to see more channels."); musicRow->addWidget(patternChannel_);
    layout->addWidget(musicControls_); musicControls_->hide();
    auto musicChanged = [this] {
        ScopeCard options;
        options.horizontal = rollDirection_->currentIndex() == 1;
        options.commands = patternCommands_->isChecked();
        options.keyboardSize = rollKeyboardSize_->value();
        options.firstChannel = patternChannel_->value() - 1;
        if (onMusicChanged) onMusicChanged(options);
    };
    connect(rollDirection_, &QComboBox::currentIndexChanged, this, musicChanged);
    connect(rollKeyboardSize_, &QSpinBox::valueChanged, this, musicChanged);
    connect(patternCommands_, &QCheckBox::toggled, this, musicChanged);
    connect(patternChannel_, &QSpinBox::valueChanged, this, musicChanged);
    connect(cardView_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0 && onViewChanged) onViewChanged(ScopeView(cardView_->itemData(index).toInt()));
    });
    auto spectrumChanged = [this] {
        if (onSpectrumChanged) onSpectrumChanged(spectrumStyle_->currentIndex() == 1, spectrumGrid_->isChecked());
    };
    connect(spectrumStyle_, &QComboBox::currentIndexChanged, this, spectrumChanged);
    connect(spectrumGrid_, &QCheckBox::toggled, this, spectrumChanged);
    auto colors = new QPushButton("Colors..."); colors->setObjectName("scopeColors");
    colors->setToolTip("Edit the selected channel's colors"); layout->addWidget(colors);
    connect(colors, &QPushButton::clicked, this, [this] { if (onColorsRequested) onColorsRequested(); });
    layout->addWidget(viewHelp_); layout->addWidget(keyboardHint_); layout->addStretch();
}
void ScopeCardInspector::setCard(const ScopeCard *card, const QString &name, ScopeViewSupport support,
                                 const QString &keyboardHint, int sources) {
    const bool valid = card != nullptr;
    selectedCard_->setText(name); selectedCard_->setToolTip(name);
    QSignalBlocker block(cardView_);
    cardView_->clear();
    for (auto kind : ScopeViews)
        if (support.supports(kind)) cardView_->addItem(scopeViewName(kind), int(kind));
    cardView_->setEnabled(valid); cardView_->setCurrentIndex(valid ? cardView_->findData(int(card->kind)) : 0);
    cardView_->setToolTip("Keyboard shows pitches from chip or MIDI data where available; other sources estimate one pitch from audio.");
    const QSignalBlocker styleBlock(spectrumStyle_), gridBlock(spectrumGrid_);
    spectrumControls_->setVisible(valid && card->kind == ScopeView::Spectrum);
    spectrumStyle_->setCurrentIndex(valid && card->spectrumBars ? 1 : 0);
    spectrumGrid_->setChecked(!valid || card->spectrumGrid);
    const int source = valid ? card->source : -1;
    keyboardHint_->setText(keyboardHint);
    const bool roll = valid && card->kind == ScopeView::PianoRoll;
    const bool pattern = valid && card->kind == ScopeView::Pattern;
    const QSignalBlocker directionBlock(rollDirection_), sizeBlock(rollKeyboardSize_),
        commandsBlock(patternCommands_), channelBlock(patternChannel_);
    musicControls_->setVisible(roll || pattern); rollDirection_->setVisible(roll); patternCommands_->setVisible(pattern);
    rollKeyboardSize_->setVisible(roll);
    patternChannel_->setVisible(pattern && source == ScopeCards::FullMix);
    rollDirection_->setCurrentIndex(valid && card->horizontal ? 1 : 0);
    rollKeyboardSize_->setValue(valid ? card->keyboardSize : 25);
    patternCommands_->setChecked(valid && card->commands);
    patternChannel_->setRange(1, std::max(1, sources));
    patternChannel_->setValue(valid ? card->firstChannel + 1 : 1);
    if (roll || pattern) {
        keyboardHint_->setText(roll ?
            "MIDI key presses, colored by instrument. Pedal, pitch bend and sound tails are not drawn. Enlarge the card for more space.":
            "Live pattern rows: note and instrument. Volume/effects use libxmp's normalized commands. Enlarge Full mix to see more channels.");
    }
    const bool hasHelp = valid && (card->kind == ScopeView::Keyboard || roll || pattern);
    viewHelp_->setVisible(hasHelp); keyboardHint_->setVisible(hasHelp && viewHelp_->isChecked());
}
QSize ScopeCardInspector::sizeHint() const {
    if (!widget()) return QScrollArea::sizeHint();
    auto hint = widget()->sizeHint();
    if (widget()->hasHeightForWidth()) hint.setHeight(widget()->heightForWidth(viewport()->width()));
    return hint;
}
bool ScopeCardInspector::eventFilter(QObject *object, QEvent *event) {
    // Context controls and expanded help change the space needed below the list.
    if (object == widget() && event->type() == QEvent::LayoutRequest) updateGeometry();
    return QScrollArea::eventFilter(object, event);
}
