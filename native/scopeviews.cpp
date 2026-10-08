#include "window.h"
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
    switch (kind) {
    case ViewKind::Spectrum: return "Spectrum";
    case ViewKind::Keyboard: return "Keyboard";
    default: return "Waveform";
    }
}
bool ScopeWindow::outputEnabled() const {
    if (newFile_) return false;
    for (int id : visibleChannels()) if (sourceFor(id) == ScopeTheme::FullMix) return true;
    return false;
}
void ScopeWindow::updateViewEditor() {
    const auto item = channelList_->currentItem();
    const int id = item ? item->data(Qt::UserRole).toInt() : -1;
    const bool valid = id >= 0 && id < cards_.size();
    QSignalBlocker block(cardView_);
    cardView_->setEnabled(valid); cardView_->setCurrentIndex(valid ? int(cards_[id].kind) : 0);
    cardView_->setToolTip("Keyboard shows all pitches supplied by a source. Chip notes are used where available; other sources estimate one pitch from audio.");
    const QSignalBlocker styleBlock(spectrumStyle_), gridBlock(spectrumGrid_);
    spectrumControls_->setVisible(valid && cards_[id].kind == ViewKind::Spectrum);
    spectrumStyle_->setCurrentIndex(valid && cards_[id].spectrumBars ? 1 : 0);
    spectrumGrid_->setChecked(!valid || cards_[id].spectrumGrid);
    const int source = valid ? sourceFor(id) : -1;
    const bool exact = source >= 0 && source < 32 && (state_.info.tonalMask & (1u << source));
    keyboardHint_->setVisible(valid && cards_[id].kind == ViewKind::Keyboard);
    keyboardHint_->setText(exact ? "Notes from chip oscillators. All active tonal pitches are shown." : "Estimated from audio: one pitch. Chords, noise and drums may be inaccurate.");
    addCard_->setEnabled(state_.valid && order_.size() < 128);
    removeCard_->setEnabled(valid);
    removeCard_->setToolTip("Remove this card without muting its source. Add card... or Reset can bring it back.");
}
void ScopeWindow::addCard() {
    if (!state_.valid || order_.size() >= 128) return;
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
    for (auto type : {ViewKind::Waveform, ViewKind::Spectrum, ViewKind::Keyboard}) kind->addItem(viewName(type), int(type));
    kind->setCurrentIndex((sourceFor(selectedId) < 32 && (state_.info.tonalMask & (1u << sourceFor(selectedId)))) ? 2 : 1);
    form->addRow("View", kind);
    auto note = new QLabel("Add another view of a channel or the final mix.\nCards share their source's colors.\nKeyboard shows all available chip notes, or estimates one pitch from audio.");
    note->setWordWrap(true); layout->addWidget(note);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("Add");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto path = state_.info.path;
    if (dialog.exec() != QDialog::Accepted || state_.info.path != path || newFile_) return;
    int id = ScopeTheme::FullMix+1;
    while (order_.contains(id)) ++id;
    if (id >= cards_.size()) { cards_.resize(id+1); hidden_.resize(id+1); spans_.resize(id+1); }
    cards_[id] = {source->currentData().toInt(), ViewKind(kind->currentData().toInt())};
    hidden_[id] = false; spans_[id] = QSize(1, 1);
    order_.insert(std::max(0, int(order_.indexOf(selectedId))+1), id);
    rebuildList(); channelList_->setCurrentRow(order_.indexOf(id)); layoutChanged(); updateViewEditor();
}
void ScopeWindow::paintSpectrumAxes(QPainter &p, int card, const QRectF &plot, bool stereo) {
    if (!cards_[card].spectrumGrid) return;
    const auto colors = theme_.colors(sourceFor(card));
    p.save(); p.setClipRect(plot, Qt::IntersectClip); p.setFont(QFont("Segoe UI", 7));
    for (int side = 0; side < (stereo ? 2 : 1); ++side) {
        const double top = plot.top()+plot.height()*side/(stereo ? 2 : 1);
        const double bottom = top+plot.height()/(stereo ? 2 : 1)-16;
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
    const bool exact = source < 32 && (state_.info.tonalMask & (1u << source));
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
            notes << QString("%1%2").arg(names[(midi%12+12)%12]).arg(int(std::floor(midi/12.))-1);
            outside |= midi < 24 || midi > 107;
        }
        text = notes.join(" + ");
        if (keysDown.size() == 1) text += QString("  |  %1 Hz").arg(keysDown.first(), 0, 'f', 1);
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
    if (source < 32 && (state_.info.tonalMask & (1u << source))) return frame_.noteHz.value(source, QVector<float>{-1});
    const auto& estimate = pitchStates_[source];
    if (estimate.generation != state_.generation || estimate.hz < 0) return {-1};
    return estimate.hz > 0 ? QVector<float>{estimate.hz} : QVector<float>{};
}
void ScopeWindow::updateKeyboardNotes() {
    for (int card : displayedChannels()) {
        const int source = sourceFor(card);
        if (viewFor(card) != ViewKind::Keyboard || !channelReady(card) ||
            (source < 32 && (state_.info.tonalMask & (1u << source)))) continue;
        const bool mix = source == ScopeTheme::FullMix;
        const int position = mix ? output_.positionMs : frame_.positionMs;
        const quint64 sequence = mix ? output_.sequence : 0;
        auto& estimate = pitchStates_[source];
        if (estimate.generation == state_.generation && estimate.position >= 0 && position >= estimate.position &&
            (position-estimate.position < 33 || (position == estimate.position && sequence == estimate.sequence))) continue;
        // Analysis is bounded to ~30 Hz per source, reused by all its cards.
        // Read actual stereo sides even when the visual Stereo checkbox is off.
        const auto& left = mix ? output_.left : frame_.left.value(source, frame_.channels[source]);
        const auto& right = mix ? output_.right : frame_.right.value(source);
        estimate.hz = scopePitch(left, right);
        estimate.generation = state_.generation; estimate.position = position; estimate.sequence = sequence;
    }
}
