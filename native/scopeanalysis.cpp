#include "scopeanalysis.h"
#include "spectrum.h"

const ScopeWave &ScopeAnalysis::waveform(int source, ScopeAnalysisKey key, const QVector<float> &left,
                                       const QVector<float> &right, const WaveOptions &options) {
    auto &state = waves_[source];
    // Keep trigger/envelope history between snapshots in the same stream.
    if (state.key.generation != key.generation || state.key.stereo != key.stereo) state = {};
    if (state.key.position != key.position || state.key.sequence != key.sequence) {
        state.wave.update(left, right, key.position, options);
        state.key = key;
    }
    return state.wave;
}
const ScopeAnalysis::Spectrum &ScopeAnalysis::spectrum(int source, ScopeAnalysisKey key, const QVector<float> &left,
                                                       const QVector<float> &right) {
    auto &state = spectra_[source];
    if (state.key != key) {
        state.bins.left = scopeSpectrum(left);
        state.bins.right = key.stereo ? scopeSpectrum(right) : state.bins.left;
        state.key = key;
    }
    return state.bins;
}
void ScopeAnalysis::estimatePitch(int source, ScopeAnalysisKey key, const QVector<float> &left, const QVector<float> &right) {
    auto &state = pitches_[source];
    // Preserve the existing ~30 Hz cadence, independently of presentation FPS
    // and stereo display. A rewind or generation change bypasses the throttle.
    if (state.key.generation == key.generation && state.key.position >= 0 && key.position >= state.key.position &&
        (key.position - state.key.position < 33 || (key.position == state.key.position && key.sequence == state.key.sequence))) return;
    state.hz = scopePitch(left, right);
    state.key = key;
}
float ScopeAnalysis::pitch(int source, quint64 generation) const {
    const auto &state = pitches_[source];
    return state.key.generation == generation ? state.hz : -1;
}
void ScopeAnalysis::clear() { clearWaves(); spectra_ = {}; pitches_ = {}; }
void ScopeAnalysis::clearWaves() { waves_ = {}; }
void ScopeAnalysis::clearWave(int source) { waves_[source] = {}; }
