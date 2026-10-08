#include "window.h"
#include "palette.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
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
    for (int id : visibleChannels()) if (sourceFor(id) == ScopeTheme::FullMix && viewFor(id) != ViewKind::Keyboard) return true;
    return false;
}
void ScopeWindow::updateViewEditor() {
    const auto item = channelList_->currentItem();
    const int id = item ? item->data(Qt::UserRole).toInt() : -1;
    const bool valid = id >= 0 && id < cards_.size();
    QSignalBlocker block(cardView_);
    cardView_->setEnabled(valid); cardView_->setCurrentIndex(valid ? int(cards_[id].kind) : 0);
    cardView_->setToolTip("Change the selected card. Keyboard notes currently support NES pulse and triangle voices.");
    addCard_->setEnabled(state_.valid && order_.size() < 128);
    removeCard_->setEnabled(valid && id > ScopeTheme::FullMix);
    removeCard_->setToolTip("Remove an added card. Original channel cards can be hidden with their checkbox.");
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
    auto note = new QLabel("Add another view of a channel or the final mix.\nCards share their source's colors.\nKeyboard: NES pulse and triangle voices only.");
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
    const bool supported = source < 32 && (state_.info.tonalMask & (1u << source));
    const float hz = supported && channelReady(card) ? frame_.noteHz.value(source, -1) : -1;
    const int midi = hz > 0 ? qRound(69+12*std::log2(hz/440.)) : -1;
    const auto colors = theme_.colors(source);
    QColor ink = colors.waveform; if (muted) ink.setAlpha(115);
    QString text;
    if (!supported) text = source == ScopeTheme::FullMix ? "Keyboard needs a single tonal voice" : "Notes unavailable for this channel";
    else if (hz < 0) text = state_.busy || !channelReady(card) ? "Preparing..." : "Waiting for note data...";
    else if (hz == 0) text = "No note";
    else {
        const QStringList names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        text = QString("%1%2  |  %3 Hz").arg(names[(midi%12+12)%12]).arg(int(std::floor(midi/12.))-1).arg(hz, 0, 'f', 1);
        if (midi < 24 || midi > 107) text += " (outside range)";
    }
    p.save(); p.setClipRect(plot, Qt::IntersectClip); p.setFont(QFont("Segoe UI", 8)); p.setPen(colors.label);
    p.drawText(QRectF(plot.left(), plot.top(), plot.width(), 18), Qt::AlignCenter, text);
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
                if (note == midi) fill = ink;
                else if (!supported) fill.setAlpha(90);
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
