#include "spectrum.h"
#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
namespace {
constexpr double Pi = 3.14159265358979323846;
template<size_t Size> void fft(std::array<std::complex<double>, Size>& data, bool inverse = false) {
    constexpr int N = int(Size);
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= N; length <<= 1) {
        const auto step = std::polar(1., (inverse ? 2 : -2)*Pi/length);
        for (int offset = 0; offset < N; offset += length) {
            std::complex<double> phase(1, 0);
            for (int j = 0; j < length/2; ++j) {
                auto a = data[offset+j], b = data[offset+j+length/2]*phase;
                data[offset+j] = a+b; data[offset+j+length/2] = a-b; phase *= step;
            }
        }
    }
    if (inverse) for (auto& value : data) value /= N;
}
}
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
    fft(data);
    QVector<float> result(N/2+1, -80);
    for (int i = 1; i <= N/2; ++i)
        result[i] = float(std::clamp(20*std::log10(std::max(1e-8, std::abs(data[i])*4/(N-1))), -80., 0.));
    return result;
}

float scopePitch(const QVector<float>& left, const QVector<float>& right) {
    constexpr int N = 2048, MaxLag = 735; // 30 Hz at 22050 Hz after 2:1 averaging.
    auto energy = [](const QVector<float>& row) {
        double sum = 0, squares = 0; const int n = std::min(4096, int(row.size()));
        for (int i = int(row.size())-n; i < row.size(); ++i) { sum += row[i]; squares += double(row[i])*row[i]; }
        return n ? std::max(0., squares/n-(sum/n)*(sum/n)) : 0.;
    };
    const double le = energy(left), re = energy(right);
    if (std::max(le, re) < 16) return 0;
    const auto& pcm = re > le ? right : left;
    if (pcm.size() < N*2) return -1;
    std::array<double, N+1> squareSum{};
    std::array<std::complex<double>, N*2> data{};
    double mean = 0; const int start = int(pcm.size())-N*2;
    for (int i = start; i < pcm.size(); ++i) mean += pcm[i];
    mean /= N*2;
    for (int i = 0; i < N; ++i) {
        const double value = ((double(pcm[start+i*2])+pcm[start+i*2+1])*.5-mean)/32768.;
        data[i] = value; squareSum[i+1] = squareSum[i]+value*value;
    }
    fft(data);
    for (auto& bin : data) bin = std::norm(bin);
    fft(data, true);
    // Cumulative-normalized squared difference (YIN), computed from FFT
    // autocorrelation and prefix energies. Padding prevents circular wraparound.
    std::array<double, MaxLag+1> difference{}; difference[0] = 1;
    double cumulative = 0;
    for (int lag = 1; lag <= MaxLag; ++lag) {
        const double d = std::max(0., squareSum[N-lag]+squareSum[N]-squareSum[lag]-2*data[lag].real())/(N-lag);
        cumulative += d; difference[lag] = cumulative > 1e-15 ? d*lag/cumulative : 1;
    }
    int lag = 6;
    for (; lag < MaxLag; ++lag) if (difference[lag] < .2) {
        while (lag < MaxLag && difference[lag+1] < difference[lag]) ++lag;
        break;
    }
    if (lag >= MaxLag) return -1;
    const double a = difference[lag-1], b = difference[lag], c = difference[lag+1];
    const double denominator = a-2*b+c;
    const double correction = denominator > 1e-10 ? std::clamp(.5*(a-c)/denominator, -.5, .5) : 0;
    return float(22050./(lag+correction));
}
