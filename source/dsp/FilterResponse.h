#pragma once

#include "dsp/MultimodeFilter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace scratchpad::dsp
{

/** The magnitude response of the filter at `hz`, computed from its settings rather than measured: the
    editor draws it as the filter curve. It follows the same maths as `SvfFilter` and `LadderFilter`
    (same resonance mapping, same frequency warping) but ignores their saturation, so it is the
    small-signal response. Linear gain, not dB. */
inline float filterMagnitude (FilterMode mode, float hz, float cutoffHz, float resonance, double sampleRate = 48000.0)
{
    const double res = std::clamp ((double) resonance, 0.0, 1.0);
    const double cutoff = std::min ((double) cutoffHz, 0.45 * sampleRate);
    const double w = std::numbers::pi * std::min ((double) hz, 0.499 * sampleRate) / sampleRate;

    // Frequency as a ratio of the cutoff on the warped (analogue-equivalent) axis.
    const double x = std::tan (w) / std::tan (std::numbers::pi * cutoff / sampleRate);
    const std::complex<double> jx (0.0, x);

    if (mode == FilterMode::lp24)
    {
        // Four one-pole stages in a loop with feedback gain k.
        const double k = 4.1 * res;
        const auto stage = 1.0 + jx;
        const auto denominator = stage * stage * stage * stage + k;
        return (float) (1.0 / std::abs (denominator));
    }

    const double k = 1.41421356 * (1.0 - res) - 0.02 * res * res * res * res;
    const auto denominator = 1.0 - x * x + k * jx;
    const double d = std::abs (denominator);

    switch (mode)
    {
        case FilterMode::lp12: return (float) (1.0 / d);
        case FilterMode::hp12: return (float) (x * x / d);
        case FilterMode::bp12: return (float) (x / d);
        case FilterMode::notch: return (float) (std::abs (1.0 - x * x) / d);
        case FilterMode::comb: return 1.0f;
        case FilterMode::lp24: break;
    }

    return 1.0f;
}

} // namespace scratchpad::dsp
