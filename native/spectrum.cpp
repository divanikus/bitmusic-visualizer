#include "spectrum.h"
#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
QVector<float> scopeSpectrum(const QVector<float>& pcm) {
    constexpr int N = 2048;
    constexpr double Pi = 3.14159265358979323846;
    std::array<std::complex<double>, N> data{};
    double mean = 0;
    const int count = std::min(N, int(pcm.size()));
    for (int i = 0; i < count; ++i) mean += pcm[pcm.size()-count+i];
    if (count) mean /= count;
    for (int i = 0; i < count; ++i) {
        const int at = N-count+i;
        const double sample = (pcm[pcm.size()-count+i]-mean)/32768.0;
        data[at] = sample * (.5-.5*std::cos(2*Pi*at/(N-1)));
    }
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= N; length <<= 1) {
        const auto step = std::polar(1., -2*Pi/length);
        for (int offset = 0; offset < N; offset += length) {
            std::complex<double> phase(1, 0);
            for (int j = 0; j < length/2; ++j) {
                auto a = data[offset+j], b = data[offset+j+length/2]*phase;
                data[offset+j] = a+b; data[offset+j+length/2] = a-b; phase *= step;
            }
        }
    }
    QVector<float> result(N/2+1, -80);
    for (int i = 1; i <= N/2; ++i)
        result[i] = float(std::clamp(20*std::log10(std::max(1e-8, std::abs(data[i])*4/(N-1))), -80., 0.));
    return result;
}
