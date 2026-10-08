#pragma once
#include <QVector>
// One-sided peak-amplitude spectrum of the most recent 2048 PCM samples.
// Values are dBFS, clamped to [-80, 0]. No normalization to the loudest bin.
QVector<float> scopeSpectrum(const QVector<float>& pcm);
