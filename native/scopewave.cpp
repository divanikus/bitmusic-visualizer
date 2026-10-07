#include "scopewave.h"
#include <algorithm>
#include <cmath>

float ScopeWave::sample(const QVector<float> &row, float index) {
    const int i = std::clamp(int(index), 0, int(row.size()) - 1);
    const int next = std::min(i + 1, int(row.size()) - 1);
    return row[i] + (row[next] - row[i]) * (index - i);
}
void ScopeWave::update(const QVector<float> &left, const QVector<float> &right, int ms, const WaveOptions &options) {
    if (left.size() != 4096 || right.size() != 4096) return;
    const bool reset = previousMs_ < 0 || ms < previousMs_ || ms - previousMs_ > 500;
    if (reset) { referenceValid_ = false; triggerSide = 0; }
    double energies[2]{};
    for (int i = 1024; i < 3072; ++i) { energies[0] += double(left[i])*left[i]; energies[1] += double(right[i])*right[i]; }
    // Keep one trigger source through small panning/level changes. Silent sides
    // cannot lock the other side out; both lanes always share the chosen start.
    const int other = 1 - triggerSide;
    if (energies[other] > energies[triggerSide] * (reset ? 1 : 4)) {
        triggerSide = other; referenceValid_ = false;
    }
    const auto &trigger = triggerSide ? right : left;
    float triggerPeak = 0;
    for (float value : trigger) triggerPeak = std::max(triggerPeak, std::abs(value));
    const float threshold = std::max(8.f, triggerPeak * .03f);
    start = 1024;
    if (options.trigger != WaveOptions::Off) {
        bool armed = false, found = false;
        float pending = -1;
        double best = -1e30;
        int candidates = 0;
        // Arm before the search window too, so a crossing at its left edge
        // does not disappear merely because its negative lobe precedes it.
        for (int i = 768; i < 2112; ++i) {
            if (trigger[i] < -threshold) { armed = true; pending = -1; }
            const bool crossing = i >= 1024 && i < 2048 && trigger[i-1] <= 0 && trigger[i] > 0;
            if (crossing && (armed || options.trigger == WaveOptions::Rising))
                pending = (i - 1) - trigger[i-1] / (trigger[i] - trigger[i-1]);
            // Require both lobes to exceed the noise threshold. Keep the actual
            // zero crossing as the start, not the later confirmation sample.
            if (pending < 0 || (options.trigger == WaveOptions::Stable && trigger[i] < threshold)) continue;
            const float candidate = pending; pending = -1; armed = false;
            if (options.trigger == WaveOptions::Rising || !referenceValid_) { start = candidate; found = true; break; }
            double dot = 0, power = 0;
            for (int j = 0; j < 64; ++j) {
                const double v = sample(trigger, candidate + j * 16);
                dot += reference_[j] * v; power += v * v;
            }
            // A weak early-position preference resolves equivalent periods;
            // normalized correlation keeps a changing amplitude from moving phase.
            const double score = dot / std::sqrt(std::max(1., power)) - .002 * (candidate - 1024) / 1024;
            if (score > best) { best = score; start = candidate; found = true; }
            if (++candidates == 64) break; // Bounded work even on broadband noise.
        }
        referenceValid_ = found && triggerPeak > threshold;
    } else referenceValid_ = false;
    if (referenceValid_) {
        double power = 0;
        for (int j = 0; j < 64; ++j) { reference_[j] = sample(trigger, start + j * 16); power += double(reference_[j])*reference_[j]; }
        const float norm = float(std::sqrt(std::max(1., power)));
        for (float &v : reference_) v /= norm;
    }
    float current = 1200;
    for (int i = int(start); i <= std::min(4095, int(std::ceil(start)) + 2047); ++i)
        current = std::max({current, std::abs(left[i]), std::abs(right[i])});
    if (options.scale == WaveOptions::Fixed) peak = 32768;
    else if (options.scale == WaveOptions::Instant || reset || current >= peak) {
        peak = current; holdUntil_ = ms + options.holdMs;
    } else {
        const int elapsed = std::max(0, ms - std::max(previousMs_, holdUntil_));
        peak = current + (peak - current) * std::exp(-float(elapsed) / std::max(1, options.releaseMs));
    }
    previousMs_ = ms;
}
