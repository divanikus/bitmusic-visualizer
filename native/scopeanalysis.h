#pragma once
#include "scopewave.h"
#include "voicemask.h"

struct ScopeAnalysisKey {
    quint64 generation = 0, sequence = 0;
    int position = -1;
    bool stereo = false;
    bool operator==(const ScopeAnalysisKey &other) const {
        return generation == other.generation && sequence == other.sequence &&
            position == other.position && stereo == other.stereo;
    }
    bool operator!=(const ScopeAnalysisKey &other) const { return !(*this == other); }
};

// GUI-thread signal analysis, shared by every card of a source. Geometry,
// painting, transport snapshots and persistence remain outside this cache.
class ScopeAnalysis {
public:
    struct Spectrum { QVector<float> left, right; };
    const ScopeWave &waveform(int source, ScopeAnalysisKey key, const QVector<float> &left,
                             const QVector<float> &right, const WaveOptions &options);
    const Spectrum &spectrum(int source, ScopeAnalysisKey key, const QVector<float> &left,
                             const QVector<float> &right);
    void estimatePitch(int source, ScopeAnalysisKey key, const QVector<float> &left, const QVector<float> &right);
    float pitch(int source, quint64 generation) const;
    void clear();
    void clearWaves();
    void clearWave(int source);
private:
    struct WaveState { ScopeAnalysisKey key; ScopeWave wave; };
    struct SpectrumState { ScopeAnalysisKey key; Spectrum bins; };
    struct PitchState { ScopeAnalysisKey key; float hz = -1; };
    std::array<WaveState, VoiceMask::Capacity + 1> waves_{};
    std::array<SpectrumState, VoiceMask::Capacity + 1> spectra_{};
    std::array<PitchState, VoiceMask::Capacity + 1> pitches_{};
};
