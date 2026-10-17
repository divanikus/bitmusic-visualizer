#pragma once
#include "scopegrid.h"
#include "scopepreset.h"
#include "voicemask.h"
#include <array>

// Values are part of the version-1 preset format.
enum class ScopeView { Waveform = 0, Spectrum = 1, Keyboard = 2, PianoRoll = 3, Pattern = 4 };
inline constexpr std::array<ScopeView, 5> ScopeViews{
    ScopeView::Waveform, ScopeView::Spectrum, ScopeView::Keyboard, ScopeView::PianoRoll, ScopeView::Pattern};
QString scopeViewName(ScopeView view);
bool scopeViewNeedsPcm(ScopeView view);
struct ScopeViewSupport {
    bool pianoRoll = false, pattern = false;
    bool supports(ScopeView view) const;
    ScopeView nextView(ScopeView current) const;
};

struct ScopeCard {
    int source = 0;
    ScopeView kind = ScopeView::Waveform;
    bool spectrumBars = false, spectrumGrid = true;
    bool horizontal = false, commands = false;
    int firstChannel = 0;
    int keyboardSize = 25;
    bool hidden = false;
    QSize span{1, 1};
    bool levels = false;
    bool autoHidden = false; // Runtime provenance; never saved in a preset.
};

struct ScopeCaptureRequest {
    VoiceMask channels;
    bool output = false;
};

// Card IDs remain stable through reorder/hide/remove. Original IDs match their
// source; extra cards use IDs above FullMix. Only order() contains active cards.
// Analysis, theme colors and audio mutes belong to sources, never to card IDs.
class ScopeCards {
public:
    static constexpr int FullMix = VoiceMask::Capacity;
    static constexpr int Maximum = ScopePreset::MaximumCards;
    const QVector<int> &order() const { return order_; }
    void setOrder(const QVector<int> &order) { order_ = order; }
    int size() const { return int(cards_.size()); }
    ScopeCard &operator[](int id) { return cards_[id]; }
    const ScopeCard &operator[](int id) const { return cards_[id]; }
    int sourceFor(int id) const { return id >= 0 && id < size() ? cards_[id].source : id; }
    ScopeView viewFor(int id) const { return id >= 0 && id < size() ? cards_[id].kind : ScopeView::Waveform; }
    void reset(int sources, bool output, VoiceMask empty, bool levels = false);
    void setAllLevels(bool enabled);
    Qt::CheckState levelsState(bool fallback) const;
    int add(const ScopeCard &card, int after);
    void remove(int id);
    void setHidden(int id, bool hidden);
    void showAll();
    bool hideEmpty(VoiceMask empty);
    void toggleSourceVisibility(int source);
    bool cycleSourceView(int source, ScopeViewSupport support);
    void setAllViews(ScopeView view);
    ScopeCaptureRequest captureRequest(const ScopeGrid &displayed) const;
    void clampSpans(int rows, int columns);
    ScopeGrid pack(int rows, int columns) const;
    ScopeGrid pack(int rows, int columns, const QVector<int> &order, int resized = -1, QSize span = {}) const;
    QVector<PresetCard> presetCards(const SoundConfiguration &configuration) const;
    void loadPreset(const ScopePreset &preset, ScopeViewSupport support, bool legacyLevels = false);
private:
    void initialize(bool hidden);
    QVector<ScopeCard> cards_;
    QVector<int> order_;
};
