// Optional pitch-state capture for the pinned libgme. LGPL-2.1-or-later.
#include "notes.h"
#include "Blip_Buffer.h"
#include <array>
#include <algorithm>
#include <cmath>
struct BmNotes {
    struct Event { int64_t begin = 0, end = 0; std::array<float, 3> hz{}; };
    std::array<Event, 2048> history{};
    uint64_t written = 0;
    int64_t clocks = 0;
    double rate = 1789773;
    double samplesPerClock = 0;
    const void* psg = nullptr;
};
namespace { thread_local BmNotes* currentNotes = nullptr; }
BmNotes* bm_notes_create() { return new BmNotes; }
void bm_notes_delete(BmNotes* n) { delete n; }
BmNotes* bm_notes_enter(BmNotes* n) { auto old = currentNotes; currentNotes = n; return old; }
void bm_notes_reset(BmNotes* n) { if (n) { n->written = 0; n->clocks = 0; } }
bool bm_notes_active() { return currentNotes != nullptr; }
double bm_notes_clock_rate() { return currentNotes ? currentNotes->rate : 0; }
void bm_notes_bind_psg(const void* apu) { if (currentNotes) currentNotes->psg = apu; }
bool bm_notes_psg_active(const void* apu) { return currentNotes && currentNotes->psg == apu; }
void bm_notes_clock(double rate) {
    if (!currentNotes) return;
    currentNotes->rate = rate;
    // Blip_Buffer rounds its resampling factor to fixed point. The nominal
    // clock/sample-rate ratio drifts away from the PCM timeline within seconds.
    constexpr double accuracy = double(1L << BLIP_BUFFER_ACCURACY);
    currentNotes->samplesPerClock = std::floor(44100.0 / rate * accuracy + .5) / accuracy;
}
void bm_notes_periods(long begin, long end, const float* periods) {
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
    if (!n || !hz || channel < 0 || channel >= 3 || sample < 0 || n->samplesPerClock <= 0) return 0;
    // Select by the delivered PCM clock, not the emulator's buffered future.
    const double clock = std::max<int64_t>(0, sample - blip_widest_impulse_ / 2) / n->samplesPerClock;
    const auto count = std::min<uint64_t>(n->written, n->history.size());
    for (uint64_t i = 0; i < count; ++i) {
        const auto& event = n->history[(n->written - 1 - i) % n->history.size()];
        if (clock >= event.begin && clock < event.end) { *hz = event.hz[channel]; return 1; }
    }
    return 0;
}
