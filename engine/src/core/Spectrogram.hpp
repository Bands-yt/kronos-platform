#pragma once

#include <complex>
#include <cstdint>
#include <vector>

namespace engine::core {

// In-place iterative radix-2 Cooley-Tukey FFT. `data.size()` must be a power
// of two; returns false (leaving `data` untouched) otherwise.
bool fftInPlace(std::vector<std::complex<float>>& data);

struct SpectrogramSettings {
    uint32_t fftSize = 2048;
    uint32_t columns = 320;
    uint32_t rows = 128;
    float minFrequencyHz = 30.0f;
    float floorDb = -96.0f;
};

// Short-time Fourier transform of a mono buffer, Hann-windowed, resampled to
// `columns` evenly spaced frames and `rows` log-spaced frequency bands. Each
// cell holds dBFS (a full-scale sine reads ~0 dB), clamped to floorDb.
struct Spectrogram {
    uint32_t columns = 0;
    uint32_t rows = 0;
    uint32_t sampleRate = 0;
    float durationSeconds = 0.0f;
    float minFrequencyHz = 0.0f;
    float maxFrequencyHz = 0.0f;
    float floorDb = -96.0f;
    std::vector<float> db; // row-major, row 0 = lowest band

    [[nodiscard]] bool empty() const { return db.empty(); }
    [[nodiscard]] float at(uint32_t column, uint32_t row) const { return db[static_cast<size_t>(row) * columns + column]; }
    [[nodiscard]] float rowCenterHz(uint32_t row) const;
};

[[nodiscard]] Spectrogram computeSpectrogram(const std::vector<float>& samples, uint32_t sampleRate,
                                             const SpectrogramSettings& settings = {});

} // namespace engine::core
