#pragma once

#include "dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace scratchpad::dsp
{

/** Topology-preserving-transform state-variable filter (Zavalishin), giving
    12 dB/octave low-pass, high-pass and band-pass outputs at once.

    Resonance runs from 0 (a Butterworth-flat response, Q = 0.707) to 1. At 1
    the filter rings forever: the damping goes slightly negative and a soft
    clip on the band-pass state holds the oscillation at a steady level of
    about one. That self-oscillation is intended. */
class SvfFilter
{
public:
    struct Outputs { float low, high, band, notch; };

    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr;
        cutoff = -1.0f; // force a recalculation
    }

    void reset() noexcept { s1 = s2 = lastBand = 0.0f; }

    /** Cheap enough to call every sample; recalculates only when something changed. */
    void setParams (float cutoffHz, float resonance) noexcept
    {
        const float res = std::clamp (resonance, 0.0f, 1.0f);

        if (same (cutoffHz, cutoff) && same (res, lastResonance))
            return;

        cutoff = cutoffHz;
        lastResonance = res;

        g = (float) std::tan (std::numbers::pi * std::min ((double) cutoffHz, 0.45 * sampleRate) / sampleRate);

        // 1.4142 * (1 - res) is damping 1/Q; the second term takes it just
        // below zero at full resonance so the oscillation sustains.
        k = 1.41421356f * (1.0f - res) - selfOscillation * res * res * res * res;
        a1 = 1.0f / (1.0f + g * (g + k));
        dynamic = k < dynamicBelow;
    }

    Outputs process (float x) noexcept
    {
        float kNow = k, a1Now = a1;

        // Near resonance the damping rises with the band-pass level, which
        // limits a ringing filter to about +-1 however low or high the cutoff.
        if (dynamic)
        {
            kNow = k + selfOscillation * lastBand * lastBand;
            a1Now = 1.0f / (1.0f + g * (g + kNow));
        }

        const float high = (x - (kNow + g) * s1 - s2) * a1Now;
        const float band = g * high + s1;
        const float low = g * band + s2;

        s1 = g * high + band;
        s2 = g * band + low;
        lastBand = band;

        return { low, high, band, low + high };
    }

private:
    static constexpr float selfOscillation = 0.02f; // negative damping at full resonance, and the band^2 gain that cancels it at +-1
    static constexpr float dynamicBelow = 0.3f;     // only filters this close to ringing pay for the extra divide

    double sampleRate = 48000.0;
    float cutoff = -1.0f, lastResonance = -1.0f;
    float g = 0.0f, k = 1.0f, a1 = 1.0f;
    float s1 = 0.0f, s2 = 0.0f, lastBand = 0.0f;
    bool dynamic = false;
};

} // namespace scratchpad::dsp
