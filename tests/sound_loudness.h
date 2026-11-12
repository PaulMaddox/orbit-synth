#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>
#include "speaker_adjustments.h"

namespace orbit::test {
inline double speakerAdjustment(int track, int sound, int midi) {
    const auto &levels = SpeakerAdjustments[track][sound];
    double position = std::max(0.0, std::min(1.0, (midi - 48) / 12.0));
    return levels[0] + (levels[1] - levels[0]) * position;
}

// Relative speaker/microphone response measured with fixed-level reference sines.
constexpr double SpeakerCurve[][2] = {
    {80,48.61}, {100,29.53}, {130,49.49}, {165,63.31}, {200,83.54},
    {260,128.69}, {330,220.29}, {400,382.76}, {450,534.89}, {500,622.38},
    {520,663.83}, {550,752.32}, {600,994.19}, {650,1386.95}, {660,1322.90},
    {700,1632.66}, {750,2138.15}, {800,2550.75}, {900,3535.67}, {1000,4135.45},
    {1040,4265.45}, {1100,4102.32}, {1200,3552.99}, {1300,2297.19},
    {1320,1486.46}, {1400,874.46}, {1500,715.66}, {1600,2791.14},
    {1700,7248.58}, {1800,12813.27}, {1900,18198.02}, {2000,23045.16},
    {2080,18599.25}, {2100,21166.36}, {2200,9592.30}, {2220,8600.90},
    {2240,7087.30}, {2260,4872.40}, {2280,3673.60}, {2300,5534.50},
    {2320,8149.60}, {2340,9989.90}, {2355,11136.70}, {2360,11295.30},
    {2380,11308.00}, {2400,10897.90}, {2420,10657.10}, {2440,10376.90},
    {2460,9950.40}, {2480,9581.90}, {2500,9419.10}, {2600,9143.81}, {2640,8900.15},
    {2700,8898.80}, {2800,8242.28}, {2900,7686.84}, {3000,7219.78},
    {3200,6447.44}, {3400,5779.23}, {3600,8482.09}, {3800,10735.14},
    {4000,9607.89}, {4160,5835.07}, {4200,7332.56}, {4400,6384.29},
    {4600,6904.49}, {4800,4734.83}, {5000,4144.62}, {5200,2189.67},
    {5280,437.26}, {5400,1056.65}, {5600,1070.85}, {5800,1855.71}, {6000,1763.10}
};

inline double speakerResponse(double hz) {
    if (hz < 80) return 48.61 / 23045.16 * std::pow(hz / 80, 2);
    for (size_t i = 1; i < sizeof(SpeakerCurve) / sizeof(*SpeakerCurve); ++i) {
        if (hz > SpeakerCurve[i][0]) continue;
        const auto &a = SpeakerCurve[i - 1], &b = SpeakerCurve[i];
        double position = std::log(hz / a[0]) / std::log(b[0] / a[0]);
        return std::exp(std::log(a[1]) + position * std::log(b[1] / a[1])) / 23045.16;
    }
    return 1763.10 / 23045.16 * std::max(0.0, (8000 - hz) / 2000);
}

inline void transform(std::vector<std::complex<double>> &bins, bool inverse) {
    constexpr double pi = 3.14159265358979323846;
    size_t size = bins.size();
    for (size_t i = 1, j = 0; i < size; ++i) {
        size_t bit = size >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(bins[i], bins[j]);
    }
    for (size_t width = 2; width <= size; width *= 2) {
        auto rotation = std::polar(1.0, (inverse ? 2 : -2) * pi / width);
        for (size_t start = 0; start < size; start += width) {
            std::complex<double> phase = 1;
            for (size_t j = 0; j < width / 2; ++j) {
                auto a = bins[start + j], b = bins[start + j + width / 2] * phase;
                bins[start + j] = a + b;
                bins[start + j + width / 2] = a - b;
                phase *= rotation;
            }
        }
    }
    if (inverse) for (auto &bin : bins) bin /= double(size);
}

inline std::vector<double> speakerSignal(const int16_t *samples, size_t count) {
    // Minimum-phase reconstruction retains attack timing, unlike weighting a
    // short FFT window, which smears tones across the speaker's sharp notches.
    static const auto impulse = [] {
        std::vector<std::complex<double>> cepstrum(2048);
        for (size_t i = 0; i < cepstrum.size(); ++i) {
            double hz = double(std::min(i, cepstrum.size() - i)) * 16000 / cepstrum.size();
            cepstrum[i] = std::log(std::max(1e-6, speakerResponse(hz)));
        }
        transform(cepstrum, true);
        for (size_t i = 1; i < 1024; ++i) cepstrum[i] *= 2;
        std::fill(cepstrum.begin() + 1025, cepstrum.end(), 0);
        transform(cepstrum, false);
        for (auto &bin : cepstrum) bin = std::exp(bin);
        transform(cepstrum, true);
        return cepstrum;
    }();
    size_t size = 1;
    while (size < count + 1023) size *= 2;
    static std::map<size_t, std::vector<std::complex<double>>> transfers;
    auto found = transfers.find(size);
    if (found == transfers.end()) {
        std::vector<std::complex<double>> transfer(size);
        for (size_t i = 0; i < 1024; ++i) transfer[i] = impulse[i].real();
        transform(transfer, false);
        found = transfers.emplace(size, std::move(transfer)).first;
    }
    std::vector<std::complex<double>> bins(size);
    for (size_t i = 0; i < count; ++i) bins[i] = samples[i];
    transform(bins, false);
    for (size_t i = 0; i < size; ++i) bins[i] *= found->second[i];
    transform(bins, true);
    std::vector<double> result(count);
    for (size_t i = 0; i < count; ++i) result[i] = bins[i].real();
    return result;
}

inline double rms(const double *samples, size_t count) {
    double energy = 0;
    for (size_t i = 0; i < count; ++i) energy += samples[i] * samples[i];
    return std::sqrt(energy / count);
}
inline double noteLoudness(const double *samples, size_t count) {
    double loudness = 0;
    constexpr size_t window = 1280, hop = 640;
    for (size_t offset = 0; offset + window <= count; offset += hop)
        loudness = std::max(loudness, rms(samples + offset, window));
    return loudness;
}
}
