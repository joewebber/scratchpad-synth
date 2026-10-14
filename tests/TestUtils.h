#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace testutil
{

/** In-place radix-2 FFT. `x.size()` must be a power of two. */
inline void fft (std::vector<std::complex<double>>& x)
{
    const size_t n = x.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (x[i], x[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / (double) len;
        const std::complex<double> wl (std::cos (angle), std::sin (angle));

        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k];
                const auto v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Magnitude spectrum (bins 0..n/2) of a Hann-windowed signal. */
inline std::vector<double> hannSpectrum (const std::vector<float>& signal)
{
    const size_t n = signal.size();
    std::vector<std::complex<double>> x (n);

    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / (double) n);
        x[i] = (double) signal[i] * w;
    }

    fft (x);

    std::vector<double> mag (n / 2 + 1);
    for (size_t i = 0; i < mag.size(); ++i)
        mag[i] = std::abs (x[i]);

    return mag;
}

inline double midiToHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

inline double centsBetween (double measuredHz, double expectedHz)
{
    return 1200.0 * std::log2 (measuredHz / expectedHz);
}

} // namespace testutil
