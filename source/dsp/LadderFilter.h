#pragma once

#include "dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace scratchpad::dsp
{

/** Four-pole (24 dB/octave) low-pass: four TPT one-pole stages in a feedback
    loop (Zavalishin's ladder), with a tanh in the feedback path.

    Resonance 0 to 1 sets the feedback. The four poles are all at the cutoff,
    so the loop turns through 180 degrees exactly there and the filter peaks
    (and, at 1, rings on its own) at the cutoff frequency. The feedback gain
    goes slightly above 4 at full resonance so the tanh, not decay, decides
    the steady self-oscillation level. */
class LadderFilter
{
public:
    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr;
        cutoff = -1.0f;
    }

    void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.0f; }

    /** Cheap enough to call every sample; recalculates only when something changed. */
    void setParams (float cutoffHz, float resonance) noexcept
    {
        const float res = std::clamp (resonance, 0.0f, 1.0f);

        if (same (cutoffHz, cutoff) && same (res, lastResonance))
            return;

        cutoff = cutoffHz;
        lastResonance = res;

        const float g = (float) std::tan (std::numbers::pi * std::min ((double) cutoffHz, 0.45 * sampleRate) / sampleRate);
        G = g / (1.0f + g);
        k = 4.1f * res;

        const float G2 = G * G;
        G4 = G2 * G2;
        solveDenominator = 1.0f / (1.0f + k * G4);
    }

    float process (float x) noexcept
    {
        // Contribution of the stage states to the last stage's output.
        const float oneMinusG = 1.0f - G;
        const float G2 = G * G;
        const float stateSum = oneMinusG * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);

        // Predict the last stage's output by solving the loop linearly, then
        // feed back only its tanh. Small signals see the exact linear loop;
        // loud ones are held in check by the saturating feedback.
        const float uLinear = (x - k * stateSum) * solveDenominator;
        const float u = x - k * std::tanh (G4 * uLinear + stateSum);

        float in = u;
        for (float& state : s)
        {
            const float v = (in - state) * G;
            const float y = v + state;
            state = y + v;
            in = y;
        }

        return in;
    }

private:
    double sampleRate = 48000.0;
    float cutoff = -1.0f, lastResonance = -1.0f;
    float G = 0.0f, G4 = 0.0f, k = 0.0f, solveDenominator = 1.0f;
    float s[4] {};
};

} // namespace scratchpad::dsp
