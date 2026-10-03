#include "core/Spectrogram.hpp"

#include <algorithm>
#include <cmath>

namespace engine::core {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isPowerOfTwo(size_t n) { return n != 0 && (n & (n - 1)) == 0; }

} // namespace

bool fftInPlace(std::vector<std::complex<float>>& data) {
    const size_t n = data.size();
    if (!isPowerOfTwo(n)) return false;

    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t start = 0; start < n; start += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> even = data[start + k];
                const std::complex<float> odd = data[start + k + len / 2] * std::complex<float>(w);
                data[start + k] = even + odd;
                data[start + k + len / 2] = even - odd;
                w *= step;
            }
        }
    }
    return true;
}

float Spectrogram::rowCenterHz(uint32_t row) const {
    if (rows == 0 || minFrequencyHz <= 0.0f) return 0.0f;
    const float t = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
    return minFrequencyHz * std::pow(maxFrequencyHz / minFrequencyHz, t);
}

Spectrogram computeSpectrogram(const std::vector<float>& samples, uint32_t sampleRate,
                               const SpectrogramSettings& settings) {
    Spectrogram out;
    const uint32_t n = settings.fftSize;
    if (samples.empty() || sampleRate == 0 || !isPowerOfTwo(n) || settings.columns == 0 || settings.rows == 0) {
        return out;
    }

    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    out.columns = settings.columns;
    out.rows = settings.rows;
    out.sampleRate = sampleRate;
    out.durationSeconds = static_cast<float>(samples.size()) / static_cast<float>(sampleRate);
    out.minFrequencyHz = std::clamp(settings.minFrequencyHz, static_cast<float>(sampleRate) / n, nyquist * 0.5f);
    out.maxFrequencyHz = nyquist;
    out.floorDb = settings.floorDb;
    out.db.assign(static_cast<size_t>(out.columns) * out.rows, settings.floorDb);

    std::vector<float> window(n);
    for (uint32_t i = 0; i < n; ++i) {
        window[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * i / static_cast<double>(n)));
    }

    // Each row covers [lo, hi) in FFT bins on a log axis; narrow low rows
    // that fall between bins take their nearest bin.
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(n);
    std::vector<std::pair<uint32_t, uint32_t>> rowBins(out.rows);
    const float ratio = out.maxFrequencyHz / out.minFrequencyHz;
    for (uint32_t r = 0; r < out.rows; ++r) {
        const float f0 = out.minFrequencyHz * std::pow(ratio, static_cast<float>(r) / out.rows);
        const float f1 = out.minFrequencyHz * std::pow(ratio, static_cast<float>(r + 1) / out.rows);
        uint32_t lo = static_cast<uint32_t>(std::floor(f0 / binHz));
        uint32_t hi = static_cast<uint32_t>(std::ceil(f1 / binHz));
        lo = std::min(lo, n / 2);
        hi = std::clamp(hi, lo + 1, n / 2 + 1);
        rowBins[r] = {lo, hi};
    }

    // A full-scale sine has peak |X| = N/4 under a Hann window.
    const float reference = static_cast<float>(n) * 0.25f;
    std::vector<std::complex<float>> frame(n);
    std::vector<float> magnitude(n / 2 + 1);
    const double hop = out.columns > 1
        ? static_cast<double>(samples.size() > n ? samples.size() - n : 0) / static_cast<double>(out.columns - 1)
        : 0.0;
    for (uint32_t c = 0; c < out.columns; ++c) {
        const size_t start = static_cast<size_t>(std::llround(hop * c));
        for (uint32_t i = 0; i < n; ++i) {
            const size_t s = start + i;
            frame[i] = std::complex<float>(s < samples.size() ? samples[s] * window[i] : 0.0f, 0.0f);
        }
        fftInPlace(frame);
        for (uint32_t k = 0; k <= n / 2; ++k) magnitude[k] = std::abs(frame[k]);

        for (uint32_t r = 0; r < out.rows; ++r) {
            float peak = 0.0f;
            for (uint32_t k = rowBins[r].first; k < rowBins[r].second; ++k) peak = std::max(peak, magnitude[k]);
            const float db = peak > 0.0f ? 20.0f * std::log10(peak / reference) : settings.floorDb;
            out.db[static_cast<size_t>(r) * out.columns + c] = std::max(db, settings.floorDb);
        }
    }
    return out;
}

} // namespace engine::core
