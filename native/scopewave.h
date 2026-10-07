#pragma once
#include <QVector>
#include <array>

struct WaveOptions {
    enum Scale { Smooth, Instant, Fixed };
    enum Trigger { Stable, Rising, Off };
    Scale scale = Smooth;
    Trigger trigger = Stable;
    int holdMs = 300, releaseMs = 1000;
    bool output = false;
};

// Presentation only: never changes PCM. One instance per card, shared by CPU/GPU.
// Call once per new scope frame, not once per repaint or resize.
class ScopeWave {
public:
    void update(const QVector<float> &left, const QVector<float> &right, int positionMs, const WaveOptions &options);
    float start = 1024, peak = 1200;
    int triggerSide = 0;
    static float sample(const QVector<float> &row, float index);
private:
    int previousMs_ = -1, holdUntil_ = 0;
    bool referenceValid_ = false;
    std::array<float, 64> reference_{};
};
