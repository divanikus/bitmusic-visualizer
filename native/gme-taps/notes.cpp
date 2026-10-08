// Optional pitch-state capture for the pinned libgme. LGPL-2.1-or-later.
#include "notes.h"
#include <array>
#include <algorithm>
struct BmNotes {
    struct Event { int64_t begin = 0, end = 0; std::array<float, 3> hz{}; };
    std::array<Event, 2048> history{};
    uint64_t written = 0;
    int64_t clocks = 0;
    double rate = 1789773;
};
namespace { thread_local BmNotes* currentNotes = nullptr; }
BmNotes* bm_notes_create() { return new BmNotes; }
void bm_notes_delete(BmNotes* n) { delete n; }
BmNotes* bm_notes_enter(BmNotes* n) { auto old = currentNotes; currentNotes = n; return old; }
void bm_notes_reset(BmNotes* n) { if (n) { n->written = 0; n->clocks = 0; } }
bool bm_notes_active() { return currentNotes != nullptr; }
void bm_notes_clock(double rate) { if (currentNotes) currentNotes->rate = rate; }
void bm_notes_nes(long begin, long end, const float* periods) {
    if (!currentNotes || end <= begin) return;
    auto& n = *currentNotes;
    BmNotes::Event event; event.begin = n.clocks + begin; event.end = n.clocks + end;
    for (int i = 0; i < 3; ++i) event.hz[i] = periods[i] > 0 ? float(n.rate / periods[i]) : 0;
    if (n.written) {
        auto& previous = n.history[(n.written - 1) % n.history.size()];
        if (previous.end == event.begin && previous.hz == event.hz) { previous.end = event.end; return; }
    }
    n.history[n.written++ % n.history.size()] = event;
}
void bm_notes_end(long clocks) { if (currentNotes) currentNotes->clocks += clocks; }
int bm_notes_read(BmNotes* n, int channel, int64_t sample, float* hz) {
    if (!n || !hz || channel < 0 || channel >= 3 || sample < 0) return 0;
    // Select by the delivered PCM clock, not the emulator's buffered future.
    const double clock = sample * n->rate / 44100.0;
    const auto count = std::min<uint64_t>(n->written, n->history.size());
    for (uint64_t i = 0; i < count; ++i) {
        const auto& event = n->history[(n->written - 1 - i) % n->history.size()];
        if (clock >= event.begin && clock < event.end) { *hz = event.hz[channel]; return 1; }
    }
    return 0;
}
