#pragma once

#include "dsp/FractionalRead.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** A stereo feedback delay of up to two seconds with a low-pass (5 kHz, one pole) in the feedback
    path, so each repeat is a little darker than the last.

    Ping-pong blends two things as `pingPong` goes from 0 to 1: the input is summed to mono and
    written only to the left line, and each line's feedback is fed to the other line. The first
    echo is then on the left, the second on the right, the third on the left, and so on.

    The time is smoothed over 80 ms, so changing it glides the pitch of the repeats like tape
    rather than clicking. Feedback, ping-pong and mix are smoothed over 20 ms. The wet signal is
    the delay lines' output alone; mix crossfades from the dry input (mix 0 returns it unchanged,
    bit for bit). */
class Delay
{
public:
    static constexpr float maxSeconds = 2.0f;
    static constexpr float minDelaySamples = 2.0f; // the interpolator reads one sample either side

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        size = (int) std::ceil ((double) maxSeconds * sampleRate) + 8;

        for (auto& buffer : buffers)
            buffer.assign ((size_t) size, 0.0f);

        feedbackCoefficient = 1.0f - std::exp (-2.0f * std::numbers::pi_v<float> * feedbackLowPassHz / (float) sampleRate);

        timeSamples.reset (sampleRate, 0.08);

        for (auto* s : { &feedback, &pingPong, &mix })
            s->reset (sampleRate, 0.02);

        reset();
    }

    void reset() noexcept
    {
        for (auto& buffer : buffers)
            std::fill (buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        lowPass = {};
    }

    void setParams (float timeMs, float newFeedback, bool newPingPong, float newMix, bool jump) noexcept
    {
        const float samples = std::clamp (timeMs * 0.001f * (float) sampleRate, minDelaySamples, maxSeconds * (float) sampleRate);
        const float pp = newPingPong ? 1.0f : 0.0f;

        if (jump)
        {
            timeSamples.setCurrentAndTargetValue (samples);
            feedback.setCurrentAndTargetValue (newFeedback);
            pingPong.setCurrentAndTargetValue (pp);
            mix.setCurrentAndTargetValue (newMix);
        }
        else
        {
            timeSamples.setTargetValue (samples);
            feedback.setTargetValue (newFeedback);
            pingPong.setTargetValue (pp);
            mix.setTargetValue (newMix);
        }
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float delay = std::clamp (timeSamples.getNextValue(), minDelaySamples, (float) (size - 8));
            const float fb = feedback.getNextValue();
            const float pp = pingPong.getNextValue();
            const float m = mix.getNextValue();

            const float wetLeft = readCubic (buffers[0].data(), size, writeIndex, delay);
            const float wetRight = readCubic (buffers[1].data(), size, writeIndex, delay);

            lowPass[0] += feedbackCoefficient * (wetLeft - lowPass[0]);
            lowPass[1] += feedbackCoefficient * (wetRight - lowPass[1]);

            const float feedLeft = lowPass[0] + pp * (lowPass[1] - lowPass[0]);
            const float feedRight = lowPass[1] + pp * (lowPass[0] - lowPass[1]);

            const float dryLeft = left[i];
            const float dryRight = right[i];
            const float mono = 0.5f * (dryLeft + dryRight);

            buffers[0][(size_t) writeIndex] = dryLeft + pp * (mono - dryLeft) + fb * feedLeft;
            buffers[1][(size_t) writeIndex] = dryRight * (1.0f - pp) + fb * feedRight;

            if (++writeIndex >= size)
                writeIndex = 0;

            left[i] = dryLeft * (1.0f - m) + wetLeft * m;
            right[i] = dryRight * (1.0f - m) + wetRight * m;
        }
    }

private:
    static constexpr float feedbackLowPassHz = 5000.0f;

    double sampleRate = 48000.0;
    int size = 1;
    int writeIndex = 0;
    float feedbackCoefficient = 0.5f;

    std::array<std::vector<float>, 2> buffers;
    std::array<float, 2> lowPass {};
    juce::SmoothedValue<float> timeSamples { 16800.0f }, feedback { 0.4f }, pingPong { 0.0f }, mix { 0.35f };
};

} // namespace scratchpad::dsp
