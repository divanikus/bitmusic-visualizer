#include "scopecards.h"
#include <algorithm>

QString scopeViewName(ScopeView view) {
    switch (view) {
    case ScopeView::Spectrum: return "Spectrum";
    case ScopeView::Keyboard: return "Keyboard";
    case ScopeView::PianoRoll: return "Piano roll";
    case ScopeView::Pattern: return "Pattern";
    default: return "Waveform";
    }
}
bool ScopeViewSupport::supports(ScopeView view) const {
    return view == ScopeView::PianoRoll ? pianoRoll : view == ScopeView::Pattern ? pattern : true;
}
bool scopeViewNeedsPcm(ScopeView view) {
    return view == ScopeView::Waveform || view == ScopeView::Spectrum || view == ScopeView::Keyboard;
}
void ScopeCards::initialize(bool hidden) {
    cards_.clear(); order_.clear();
    for (int source = 0; source <= FullMix; ++source) {
        ScopeCard card; card.source = source; card.hidden = hidden;
        cards_.push_back(card);
    }
}
void ScopeCards::reset(int sources, bool output, VoiceMask empty, bool levels) {
    initialize(false);
    for (int source = 0; source < sources; ++source) order_.push_back(source);
    order_.push_back(FullMix); cards_[FullMix].hidden = !output;
    hideEmpty(empty);
    setAllLevels(levels);
}
int ScopeCards::add(const ScopeCard &card, int after) {
    if (order_.size() >= Maximum) return -1;
    int id = FullMix + 1;
    while (order_.contains(id)) ++id;
    if (id >= size()) cards_.resize(id + 1);
    cards_[id] = card;
    order_.insert(std::max(0, int(order_.indexOf(after)) + 1), id);
    return id;
}
void ScopeCards::remove(int id) {
    if (id < 0 || id >= size()) return;
    order_.removeAll(id); setHidden(id, true);
}
void ScopeCards::setHidden(int id, bool hidden) {
    auto &card = cards_[id];
    card.hidden = hidden; card.autoHidden = false;
}
void ScopeCards::showAll() {
    for (int id : order_) setHidden(id, false);
}
bool ScopeCards::hideEmpty(VoiceMask empty) {
    bool changed = false;
    for (int id : order_) {
        auto &card = cards_[id];
        const bool wasHidden = card.hidden;
        // Re-evaluate automatic hiding for this subsong; retain manual/preset hides.
        if (card.autoHidden) card.hidden = false;
        card.autoHidden = !card.hidden && empty.test(card.source);
        if (card.autoHidden) card.hidden = true;
        changed |= card.hidden != wasHidden;
    }
    return changed;
}
void ScopeCards::toggleSourceVisibility(int source) {
    bool hide = false;
    for (int id : order_) if (cards_[id].source == source && !cards_[id].hidden) hide = true;
    for (int id : order_) if (cards_[id].source == source) setHidden(id, hide);
}
bool ScopeCards::cycleSourceView(int source, ScopeViewSupport support) {
    int first = order_.contains(source) ? source : -1;
    if (first < 0) for (int id : order_) if (cards_[id].source == source) { first = id; break; }
    if (first < 0) return false;
    auto next = ScopeView((int(cards_[first].kind) + 1) % ScopeViews.size());
    while (!support.supports(next)) next = ScopeView((int(next) + 1) % ScopeViews.size());
    for (int id : order_) if (cards_[id].source == source) cards_[id].kind = next;
    return true;
}
void ScopeCards::setAllViews(ScopeView view) {
    for (int id : order_) cards_[id].kind = view;
}
void ScopeCards::setAllLevels(bool enabled) {
    for (auto &card : cards_) card.levels = enabled;
}
Qt::CheckState ScopeCards::levelsState(bool fallback) const {
    if (order_.isEmpty()) return fallback ? Qt::Checked : Qt::Unchecked;
    const bool first = cards_[order_.front()].levels;
    for (int id : order_) if (cards_[id].levels != first) return Qt::PartiallyChecked;
    return first ? Qt::Checked : Qt::Unchecked;
}
ScopeCaptureRequest ScopeCards::captureRequest(const ScopeGrid &displayed) const {
    ScopeCaptureRequest request;
    for (int id : order_) {
        const auto &card = cards_[id];
        if (!displayed.contains(id) || (!card.levels && !scopeViewNeedsPcm(card.kind))) continue;
        if (card.source == FullMix) request.output = true;
        else request.channels |= VoiceMask::bit(card.source);
    }
    return request;
}
void ScopeCards::clampSpans(int rows, int columns) {
    for (auto &card : cards_) card.span = QSize(std::clamp(card.span.width(), 1, columns), std::clamp(card.span.height(), 1, rows));
}
ScopeGrid ScopeCards::pack(int rows, int columns) const { return pack(rows, columns, order_); }
ScopeGrid ScopeCards::pack(int rows, int columns, const QVector<int> &order, int resized, QSize span) const {
    QVector<bool> hidden; QVector<QSize> spans;
    hidden.reserve(cards_.size()); spans.reserve(cards_.size());
    for (const auto &card : cards_) { hidden.push_back(card.hidden); spans.push_back(card.span); }
    if (resized >= 0 && resized < spans.size()) spans[resized] = span;
    return packScopes(rows, columns, order, hidden, spans);
}
QVector<PresetCard> ScopeCards::presetCards(const SoundConfiguration &configuration) const {
    QVector<PresetCard> result;
    for (int id : order_) {
        const auto &card = cards_[id];
        result.push_back({card.source == FullMix ? "mix" : configuration.sources.value(card.source),
            int(card.kind), card.span, !card.hidden, card.spectrumBars, card.spectrumGrid,
            card.horizontal, card.commands, card.firstChannel, card.keyboardSize, card.levels});
    }
    return result;
}
void ScopeCards::loadPreset(const ScopePreset &preset, ScopeViewSupport support, bool legacyLevels) {
    initialize(true);
    for (const auto &saved : preset.cards) {
        const int source = saved.source == "mix" ? FullMix : int(preset.configuration.sources.indexOf(saved.source));
        if (source < 0) continue;
        int id = source;
        if (order_.contains(source)) { id = size(); cards_.push_back({source}); }
        auto &card = cards_[id];
        card = {source, support.supports(ScopeView(saved.view)) ? ScopeView(saved.view) : ScopeView::Waveform,
            saved.bars, saved.grid, saved.horizontal, saved.commands,
            std::clamp(saved.firstChannel, 0, int(preset.configuration.sources.size()) - 1), saved.keyboardSize,
            !saved.visible, saved.span, saved.levels.value_or(legacyLevels)};
        order_.push_back(id);
    }
}
