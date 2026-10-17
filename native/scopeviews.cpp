#include "scopewindow.h"
#include "scopecardinspector.h"
#include "palette.h"
#include "spectrum.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <algorithm>

QString ScopeWindow::sourceName(int source) const {
    return source == ScopeTheme::FullMix ? QString("Full mix") : state_.info.voices.value(source);
}
QString ScopeWindow::viewName(ViewKind kind) const {
    return scopeViewName(kind);
}
bool ScopeWindow::viewSupported(ViewKind kind) const {
    return ScopeViewSupport{bool(state_.info.noteRoll), bool(state_.info.patterns)}.supports(kind);
}
bool ScopeWindow::outputEnabled() const {
    return captureRequest().output;
}
void ScopeWindow::cycleSourceView(int source) {
    if (!state_.valid || (source != ScopeTheme::FullMix && (source < 0 || source >= state_.info.voices.size()))) return;
    if (!cards_.cycleSourceView(source, {bool(state_.info.noteRoll), bool(state_.info.patterns)})) return;
    layoutChanged(); updateViewEditor();
}
void ScopeWindow::toggleSourceVisibility(int source) {
    if (!state_.valid || (source != ScopeTheme::FullMix && (source < 0 || source >= state_.info.voices.size()))) return;
    cards_.toggleSourceVisibility(source);
    layoutChanged(); updateViewEditor();
}
void ScopeWindow::setAllViews(ViewKind kind) {
    if(!viewSupported(kind)) return;
    cards_.setAllViews(kind);
    layoutChanged(); updateViewEditor();
}
int ScopeWindow::selectedCardId() const {
    const auto item = channelList_->currentItem();
    const int id = item ? item->data(Qt::UserRole).toInt() : -1;
    return id >= 0 && id < cards_.size() ? id : -1;
}
void ScopeWindow::updateLevelsControl() {
    const QSignalBlocker block(levels_);
    const auto state = cards_.levelsState(waveOptions_.meters);
    levels_->setTristate(state == Qt::PartiallyChecked);
    levels_->setCheckState(state);
}
void ScopeWindow::updateViewEditor() {
    const int id = selectedCardId();
    const bool valid = id >= 0;
    const int source = valid ? sourceFor(id) : -1;
    const bool exact = state_.info.tonalMask.test(source);
    const QString hint = exact ? (state_.info.mt32Resources ?
        "MT-32 played keys. Timbre tuning, pitch bend and modulation are not measured." : state_.info.midiResources ?
        "MIDI notes, sustain and channel pitch bend. Sample tuning and synthesis modulation are not measured." :
        "Notes from chip oscillators. All active tonal pitches are shown.") :
        "Estimated from audio: one pitch. Chords, noise and drums may be inaccurate.";
    cardInspector_->setCard(valid ? &cards_[id] : nullptr, valid ? channelName(id) : QString("Select a card"),
        {bool(state_.info.noteRoll), bool(state_.info.patterns)}, hint, int(state_.info.voices.size()));
    addCard_->setEnabled(state_.valid && cards_.order().size() < ScopeCards::Maximum);
    removeCard_->setEnabled(valid);
    removeCard_->setToolTip("Remove this card without muting its source. Add card... or Reset can bring it back.");
}
void ScopeWindow::addCard() {
    if (!state_.valid || cards_.order().size() >= ScopeCards::Maximum) return;
    const auto selected = channelList_->currentItem();
    const int selectedId = selected ? selected->data(Qt::UserRole).toInt() : 0;
    QDialog dialog(this); dialog.setObjectName("addCardDialog"); dialog.setWindowTitle("Add visualization card");
    dialog.setMinimumWidth(360); dialog.setStyleSheet("QDialog { background: #1b252b; }");
    auto layout = new QVBoxLayout(&dialog); auto form = new QFormLayout; layout->addLayout(form);
    auto source = new QComboBox; source->setObjectName("newCardSource"); styleCombo(source);
    for (int i = 0; i < state_.info.voices.size(); ++i) source->addItem(sourceName(i), i);
    source->addItem("Full mix", ScopeTheme::FullMix);
    source->setCurrentIndex(source->findData(sourceFor(selectedId))); form->addRow("Source", source);
    auto kind = new QComboBox; kind->setObjectName("newCardView"); styleCombo(kind);
    for (auto type : ScopeViews)
        if(viewSupported(type)) kind->addItem(viewName(type), int(type));
    kind->setCurrentIndex((sourceFor(selectedId) < VoiceMask::Capacity && (state_.info.tonalMask & (VoiceMask::bit(sourceFor(selectedId))))) ? 2 : 1);
    form->addRow("View", kind);
    auto note = new QLabel("Add another view of a channel or the final mix.\nPiano roll and Pattern show musical events where available.\nDrag card edges to make room for these views.");
    note->setWordWrap(true); layout->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("Add");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto path = state_.info.path;
    if (dialog.exec() != QDialog::Accepted || state_.info.path != path || newFile_) return;
    const int id = cards_.add({source->currentData().toInt(), ViewKind(kind->currentData().toInt())}, selectedId);
    if (id < 0) return;
    cards_[id].levels = waveOptions_.meters;
    rebuildList(); channelList_->setCurrentRow(cards_.order().indexOf(id)); layoutChanged(); updateViewEditor();
}
void ScopeWindow::paintSpectrumAxes(QPainter &p, int card, const QRectF &plot, bool stereo) {
    if (!cards_[card].spectrumGrid) return;
    const auto colors = theme_.colors(sourceFor(card));
    p.save(); p.setClipRect(plot, Qt::IntersectClip); p.setFont(QFont("Segoe UI", 7));
    for (int side = 0; side < (stereo ? 2 : 1); ++side) {
        const auto bounds = scopeSpectrumBounds(plot, stereo, side);
        const double top = bounds.top, bottom = bounds.bottom;
        if (bottom-top < 12) continue;
        p.setPen(colors.axis);
        for (int level = 0; level <= 2; ++level) {
            const double y = bottom-(bottom-top-5)*level/2;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        if (plot.width() > 180) {
            p.setPen(colors.label);
            p.drawText(QRectF(plot.left()+3, top+1, plot.width()-6, 12), Qt::AlignRight, "0 dBFS");
            p.drawText(QRectF(plot.left()+3, bottom-13, plot.width()-6, 12), Qt::AlignRight, "-80");
        }
        for (int hz : {100, 1000, 10000}) {
            const double x = plot.left()+plot.width()*std::log(double(hz)/20)/std::log(1000.);
            p.setPen(colors.axis); p.drawLine(QPointF(x, top+5), QPointF(x, bottom));
            p.setPen(colors.label);
            if (plot.width() > 120) p.drawText(QRectF(x-22, bottom+1, 44, 14), Qt::AlignCenter, hz < 1000 ? "100 Hz" : hz == 1000 ? "1 kHz" : "10 kHz");
        }
    }
    p.restore();
}
void ScopeWindow::paintKeyboard(QPainter &p, int card, const QRectF &plot, bool muted) {
    const int source = sourceFor(card);
    const bool exact = source < VoiceMask::Capacity && (state_.info.tonalMask & (VoiceMask::bit(source)));
    const auto pitches = keyboardPitches(card);
    QMap<int, float> keysDown;
    for (float hz : pitches) if (hz > 0) keysDown.insert(qRound(69+12*std::log2(hz/440.)), hz);
    const auto colors = theme_.colors(source);
    QColor ink = colors.waveform; if (muted) ink.setAlpha(115);
    QString text;
    if (!channelReady(card)) text = source == ScopeTheme::FullMix && !state_.playing && !state_.busy ? "No output" : "Preparing...";
    else if (pitches.contains(-1)) text = exact ? "Waiting for note data..." : "No stable pitch";
    else if (keysDown.isEmpty()) text = "No note";
    else {
        const QStringList names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        QStringList notes; bool outside = false;
        for (auto it = keysDown.cbegin(); it != keysDown.cend(); ++it) {
            const int midi = it.key();
            notes << QString("%1%2 · %3 Hz").arg(names[(midi%12+12)%12])
                .arg(int(std::floor(midi/12.))-1).arg(it.value(), 0, 'f', 1);
            outside |= midi < 24 || midi > 107;
        }
        text = notes.join(" + ");
        if (outside) text += " (outside range)";
    }
    if (!exact) text = "Estimated | " + text;
    p.save(); p.setClipRect(plot, Qt::IntersectClip); p.setFont(QFont("Segoe UI", 8)); p.setPen(colors.label);
    p.drawText(QRectF(plot.left(), plot.top(), plot.width(), 18), Qt::AlignCenter,
        p.fontMetrics().elidedText(text, Qt::ElideRight, int(plot.width())));
    const auto keys = plot.adjusted(0, 23, 0, 0);
    if (keys.height() < 8) { p.restore(); return; }
    const double width = keys.width()/49;
    auto black = [](int note) { const int n = note%12; return n==1 || n==3 || n==6 || n==8 || n==10; };
    for (int pass = 0; pass < 2; ++pass) {
        int white = 0;
        for (int note = 24; note < 108; ++note) {
            const bool dark = black(note);
            if (dark == bool(pass)) {
                const QRectF key(keys.left()+(white-(dark ? .32 : 0))*width, keys.top(), width*(dark ? .64 : 1), keys.height()*(dark ? .62 : 1));
                QColor fill = dark ? QColor("#26343a") : QColor("#cdd8da");
                if (keysDown.contains(note)) fill = ink;
                p.setPen(QPen(colors.background, .8)); p.setBrush(fill); p.drawRoundedRect(key.adjusted(.3, .3, -.3, -.3), 1.5, 1.5);
                if (!dark && note%12 == 0 && width >= 9 && keys.height() >= 28) {
                    p.setPen(QColor("#35484d")); p.setFont(QFont("Segoe UI", 7));
                    p.drawText(key.adjusted(0, 0, 0, -2), Qt::AlignHCenter | Qt::AlignBottom, QString("C%1").arg(note/12-1));
                }
            }
            if (!dark) ++white;
        }
    }
    p.restore();
}

QVector<float> ScopeWindow::keyboardPitches(int card) const {
    if (!channelReady(card)) return {-1};
    const int source = sourceFor(card);
    if (source < VoiceMask::Capacity && (state_.info.tonalMask & (VoiceMask::bit(source)))) return frame_.noteHz.value(source, QVector<float>{-1});
    const float estimate = analysis_.pitch(source, state_.generation);
    if (estimate < 0) return {-1};
    return estimate > 0 ? QVector<float>{estimate} : QVector<float>{};
}
void ScopeWindow::updateKeyboardNotes() {
    for (int card : displayedChannels()) {
        const int source = sourceFor(card);
        if (viewFor(card) != ViewKind::Keyboard || !channelReady(card) ||
            (source < VoiceMask::Capacity && (state_.info.tonalMask & (VoiceMask::bit(source))))) continue;
        // Analysis is bounded to ~30 Hz per source, reused by all its cards.
        // Read actual stereo sides even when the visual Stereo checkbox is off.
        const auto &left = sourceSamples(source, 0);
        // Pitch estimation treats an absent right side as empty, while meters
        // fall back to mono. Keep that distinction when selecting source PCM.
        const auto &right = sourceSamples(source, 1, MissingSide::Empty);
        analysis_.estimatePitch(source, analysisKey(source), left, right);
    }
}
