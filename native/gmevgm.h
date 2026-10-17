#pragma once
#include "engine.h"

// GME-specific metadata probes, called after inspectVgm validates the data offset.
// Timing rejects unknown commands; the stereo walk keeps its wider skip policy.
TrackBoundary gmeVgmBoundary(const QByteArray &data, qsizetype dataOffset);
bool gmeVgmStereoRouting(const QByteArray &data, qsizetype dataOffset);
