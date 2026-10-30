#pragma once

#include "dsp/FastFloor.h"
#include "dsp/FractionalRead.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** A stereo chorus: each channel sums two taps from a short delay line whose delay is swept by
    a sine LFO. The taps sit a third of a cycle apart and the right channel runs half a cycle
    ahead of the left, so a mono input comes out wide. The centre delay is 12 ms and depth sweeps
    it by up to +-8 ms. The wet signal is only the delayed taps; the mix crossfades from the dry
    input (mix 0 returns it unchanged, bit for bit). Rate only changes the speed of a phase that
    never jumps, and depth and mix are smoothed over 20 ms. */
class Chorus
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = (int) std::ceil (0.05 * sampleRate) + 8;

        for (auto& buffer : buffers)
            buffer.assign ((size_t) size, 0.0f);

        for (auto* s : { &rateHz, &depth, &mix })
            s->reset (sampleRate, 0.02);

        reset();
    }

    void reset() noexcept
    {
        for (auto& buffer : buffers)
            std::fill (buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        phase = 0.0;
    }

    void setParams (float newRateHz, float newDepth, float newMix, bool jump) noexcept
    {
        if (jump)
        {
            rateHz.setCurrentAndTargetValue (newRateHz);
            depth.setCurrentAndTargetValue (newDepth);
            mix.setCurrentAndTargetValue (newMix);
        }
        else
        {
            rateHz.setTargetValue (newRateHz);
            depth.setTargetValue (newDepth);
            mix.setTargetValue (newMix);
        }
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* channels[2] = { left, right };
        const float msToSamples = (float) sampleRate * 0.001f;

        for (int i = 0; i < numSamples; ++i)
        {
            phase += (double) rateHz.getNextValue() / sampleRate;
            phase -= fastFloor (phase);

            const float sweep = depth.getNextValue() * maxSweepMs;
            const float m = mix.getNextValue();

            for (size_t c = 0; c < 2; ++c)
            {
                auto& buffer = buffers[c];
                buffer[(size_t) writeIndex] = channels[c][i];

                float wet = 0.0f;

                for (int tap = 0; tap < 2; ++tap)
                {
                    const double p = phase + (c == 1 ? 0.5 : 0.0) + (double) tap / 3.0;
                    const float lfo = (float) std::sin (2.0 * std::numbers::pi * p);
                    wet += readCubic (buffer.data(), size, writeIndex, (centreMs + sweep * lfo) * msToSamples);
                }

                channels[c][i] = channels[c][i] * (1.0f - m) + 0.5f * wet * m;
            }

            if (++writeIndex >= size)
                writeIndex = 0;
        }
    }

private:
    static constexpr float centreMs = 12.0f;
    static constexpr float maxSweepMs = 8.0f;

    double sampleRate = 48000.0;
    int size = 1;
    int writeIndex = 0;
    double phase = 0.0;

    std::array<std::vector<float>, 2> buffers;
    juce::SmoothedValue<float> rateHz { 0.8f }, depth { 0.5f }, mix { 0.5f };
};

} // namespace scratchpad::dsp
