#pragma once

#include "dsp/FractionalRead.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** A feedback delay network reverb: eight delay lines whose outputs are damped, scaled for the
    decay time and mixed back into every line through an orthonormal 8x8 Hadamard matrix.

    - Size scales every line length together, from 0.35x to 2x the base lengths (8 to 133 ms),
      so the same knob moves the room from a cupboard to a hall. The lengths are read with cubic
      interpolation and the size is smoothed over 200 ms, so turning it glides the tail's pitch
      instead of clicking. A slow sine, a fraction of a millisecond deep and different for every
      line, keeps the tail from ringing at fixed pitches.
    - Decay is the time for the tail to fall 60 dB: each line's loop gain is
      `10^(-3 * lineSeconds / decaySeconds)`, so the whole network decays at that rate whatever
      the size. The wet level is trimmed to keep a steady signal at about the same loudness
      as the decay changes.
    - Damping is a one-pole low-pass in each line, from 40 kHz (0, effectively open) to 1.4 kHz (1). It shortens the
      tail of the treble only; the low end keeps the decay time.
    - The left and right outputs are two different sign patterns (orthogonal rows of the same
      matrix) over the same lines, so a mono input comes out decorrelated.

    The wet signal is the network's output alone; mix crossfades from the dry input (mix 0
    returns it unchanged, bit for bit). */
class Reverb
{
public:
    static constexpr int numLines = 8;
    static constexpr int maxBlock = 32;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = (int) std::ceil (0.16 * sampleRate) + 16;

        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);

        for (int j = 0; j < numLines; ++j)
        {
            baseSamples[(size_t) j] = baseMs[(size_t) j] * 0.001f * (float) sampleRate;
            modIncrement[(size_t) j] = modRateHz[(size_t) j] / sampleRate;
        }

        modDepthSamples = 0.0004f * (float) sampleRate;

        sizeSmoother.reset (sampleRate, 0.2);
        decaySmoother.reset (sampleRate, 0.1);
        dampingSmoother.reset (sampleRate, 0.05);
        mixSmoother.reset (sampleRate, 0.02);

        reset();
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);

        writeIndex = 0;
        damped = {};

        for (int j = 0; j < numLines; ++j)
            modPhase[(size_t) j] = (double) j / (double) numLines;
    }

    void setParams (float size01, float decaySeconds, float damping, float mix, bool jump) noexcept
    {
        const float decay = std::max (decaySeconds, 0.05f);

        if (jump)
        {
            sizeSmoother.setCurrentAndTargetValue (size01);
            decaySmoother.setCurrentAndTargetValue (decay);
            dampingSmoother.setCurrentAndTargetValue (damping);
            mixSmoother.setCurrentAndTargetValue (mix);
        }
        else
        {
            sizeSmoother.setTargetValue (size01);
            decaySmoother.setTargetValue (decay);
            dampingSmoother.setTargetValue (damping);
            mixSmoother.setTargetValue (mix);
        }
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        int done = 0;

        while (done < numSamples)
        {
            const int n = std::min (maxBlock, numSamples - done);
            processChunk (left + done, right + done, n);
            done += n;
        }
    }

private:
    static constexpr std::array<float, numLines> baseMs { 23.3f, 29.1f, 33.7f, 38.9f, 44.3f, 51.7f, 58.1f, 66.3f };
    static constexpr std::array<double, numLines> modRateHz { 0.31, 0.43, 0.37, 0.53, 0.29, 0.47, 0.41, 0.59 };

    // Rows 1, 2 and 3 of the 8x8 Hadamard matrix: the left output, the right output, and which lines
    // take the side signal.
    static constexpr float sign (int line, int row) noexcept
    {
        return (std::popcount ((unsigned) (line & row)) & 1) != 0 ? -1.0f : 1.0f;
    }

    static float scaleFor (float size01) noexcept { return 0.35f + 1.65f * std::clamp (size01, 0.0f, 1.0f); }

    void processChunk (float* left, float* right, int n) noexcept
    {
        // Per-chunk coefficients, from where the smoothers are now.
        const float scale = scaleFor (sizeSmoother.getCurrentValue());
        const float decay = decaySmoother.getCurrentValue();
        const float hz = 40000.0f * std::pow (0.035f, dampingSmoother.getCurrentValue());
        const float dampCoefficient = 1.0f - std::exp (-2.0f * std::numbers::pi_v<float> * hz / (float) sampleRate);

        std::array<float, numLines> gain {};
        float meanGain = 0.0f;

        for (size_t j = 0; j < (size_t) numLines; ++j)
        {
            const float seconds = baseSamples[j] * scale / (float) sampleRate;
            gain[j] = std::pow (10.0f, -3.0f * seconds / decay);
            meanGain += gain[j] / (float) numLines;
        }

        const float wetScale = 0.5f * std::clamp (std::sqrt ((1.0f - meanGain * meanGain) / 0.29f), 0.5f, 2.0f);

        decaySmoother.skip (n);
        dampingSmoother.skip (n);

        for (int i = 0; i < n; ++i)
        {
            const float lineScale = scaleFor (sizeSmoother.getNextValue());
            const float m = mixSmoother.getNextValue();

            std::array<float, numLines> y;
            float wetLeft = 0.0f, wetRight = 0.0f;

            for (int j = 0; j < numLines; ++j)
            {
                const auto js = (size_t) j;

                modPhase[js] += modIncrement[js];
                modPhase[js] -= std::floor (modPhase[js]);

                const float delay = baseSamples[js] * lineScale
                                    + modDepthSamples * (float) std::sin (2.0 * std::numbers::pi * modPhase[js]);
                const float x = readCubic (lines[js].data(), size, writeIndex, delay);

                damped[js] += dampCoefficient * (x - damped[js]);
                y[js] = damped[js] * gain[js];

                wetLeft += sign (j, 1) * y[js];
                wetRight += sign (j, 2) * y[js];
            }

            // In-place fast Walsh-Hadamard transform, then 1/sqrt(8) to make it orthonormal.
            for (int span = 1; span < numLines; span <<= 1)
                for (int start = 0; start < numLines; start += 2 * span)
                    for (int k = start; k < start + span; ++k)
                    {
                        const float a = y[(size_t) k];
                        const float b = y[(size_t) (k + span)];
                        y[(size_t) k] = a + b;
                        y[(size_t) (k + span)] = a - b;
                    }

            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]);

            for (int j = 0; j < numLines; ++j)
                lines[(size_t) j][(size_t) writeIndex] = inputGain * (mid + sign (j, 3) * side) + y[(size_t) j] * 0.35355339f;

            if (++writeIndex >= size)
                writeIndex = 0;

            left[i] = left[i] * (1.0f - m) + wetLeft * wetScale * m;
            right[i] = right[i] * (1.0f - m) + wetRight * wetScale * m;
        }
    }

    static constexpr float inputGain = 0.35355339f;

    double sampleRate = 48000.0;
    int size = 1;
    int writeIndex = 0;
    float modDepthSamples = 0.0f;

    std::array<std::vector<float>, numLines> lines;
    std::array<float, numLines> baseSamples {};
    std::array<double, numLines> modIncrement {};
    std::array<double, numLines> modPhase {};
    std::array<float, numLines> damped {};

    juce::SmoothedValue<float> sizeSmoother { 0.5f }, dampingSmoother { 0.5f }, mixSmoother { 0.3f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> decaySmoother { 2.0f };
};

} // namespace scratchpad::dsp
