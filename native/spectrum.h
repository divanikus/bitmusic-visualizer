#pragma once
#include <QVector>
#include <QRectF>
// One-sided peak-amplitude spectrum of the most recent 2048 PCM samples.
// Values are dBFS, clamped to [-80, 0]. No normalization to the loudest bin.
QVector<float> scopeSpectrum(const QVector<float>& pcm);
// Log-frequency bars, preserving the strongest FFT peak in each band.
QVector<QRectF> scopeSpectrumBars(const QVector<float>& bins, const QRectF& area);
// Dominant periodic pitch estimate; 0 = silence, -1 = no stable pitch.
// Uses the stronger stereo side to avoid L/R cancellation. Not transcription.
float scopePitch(const QVector<float>& left, const QVector<float>& right = {});
