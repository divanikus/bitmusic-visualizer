#pragma once
#include "taps.h"
#include <vector>

// One stereo tick per source. Background/NNA voices accumulate under their
// original tracker channel. Capture never writes to the upstream mix buffer.
struct XmpCapture {
    std::vector<std::vector<float>> channels;
    std::vector<int> previous;
    int source = -1, samples = 0;
    float scale = 1;
    explicit XmpCapture(int count) : channels(count) {}
};
class XmpCaptureScope {
public:
    explicit XmpCaptureScope(XmpCapture *capture);
    ~XmpCaptureScope();
private:
    XmpCapture *previous_;
};
