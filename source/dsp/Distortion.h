#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** Drive into a tanh waveshaper, run at 4x the sample rate (two half-band FIR stages from
    `juce::dsp::Oversampling`) so the harmonics it makes above Nyquist are filtered out
    rather than folded back down. Then a two-pole low-pass "tone" control and a dry/wet mix.

    Drive is a gain of `driveDb` into `tanh`. The output is scaled by `gain^-0.25`, which holds
    the loudness of a saturated signal roughly level as the drive rises (+12 dB of drive costs
    3 dB of make-up, +40 dB costs 10 dB). Tone sweeps the low-pass from 800 Hz (0) to 30 kHz,
    which is open (1).

    The oversampler delays the wet path by `getLatencySamples()`; the dry path is delayed by the
    same amount before the mix, so mix positions are aligned and mix 0 is the input, late by
    that latency, bit for bit. */
class Distortion
{
public:
    static constexpr int maxBlock = 64;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);

        latency = (int) std::lround (oversampler->getLatencyInSamples());

        for (auto& ring : dryRing)
            ring.assign ((size_t) latency + 1, 0.0f);

        for (auto* s : { &driveDb, &tone, &mix })
            s->reset (sampleRate, 0.02);

        reset();
    }

    int getLatencySamples() const noexcept { return latency; }

    void reset() noexcept
    {
        if (oversampler != nullptr)
            oversampler->reset();

        for (auto& ring : dryRing)
            std::fill (ring.begin(), ring.end(), 0.0f);

        ringIndex = 0;
        toneState = {};
        toneCoefficient = coefficientFor (tone.getCurrentValue());
    }

    void setParams (float newDriveDb, float newTone, float newMix, bool jump) noexcept
    {
        if (jump)
        {
            driveDb.setCurrentAndTargetValue (newDriveDb);
            tone.setCurrentAndTargetValue (newTone);
            mix.setCurrentAndTargetValue (newMix);
            toneCoefficient = coefficientFor (newTone);
        }
        else
        {
            driveDb.setTargetValue (newDriveDb);
            tone.setTargetValue (newTone);
            mix.setTargetValue (newMix);
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
    static constexpr float toneLowHz = 800.0f;
    static constexpr float toneHighHz = 30000.0f;

    float coefficientFor (float toneValue) const noexcept
    {
        const float hz = std::min (toneLowHz * std::pow (toneHighHz / toneLowHz, toneValue), 0.49f * (float) sampleRate);
        return 1.0f - std::exp (-2.0f * std::numbers::pi_v<float> * hz / (float) sampleRate);
    }

    void processChunk (float* left, float* right, int n) noexcept
    {
        float* channels[2] = { left, right };

        // The dry path, late by the oversampler's latency.
        const size_t ringSize = dryRing[0].size();

        for (int i = 0; i < n; ++i)
        {
            for (size_t c = 0; c < 2; ++c)
            {
                dryRing[c][ringIndex] = channels[c][i];
                dry[c][(size_t) i] = dryRing[c][(ringIndex + 1) % ringSize];
            }

            ringIndex = (ringIndex + 1) % ringSize;
        }

        juce::dsp::AudioBlock<float> block (channels, 2, (size_t) n);
        auto up = oversampler->processSamplesUp (juce::dsp::AudioBlock<const float> (block));
        float* upLeft = up.getChannelPointer (0);
        float* upRight = up.getChannelPointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float gain = std::exp (driveDb.getNextValue() * 0.11512925f);
            const float makeup = std::pow (gain, -0.25f);

            for (int j = 0; j < 4; ++j)
            {
                const size_t k = (size_t) (4 * i + j);
                upLeft[k] = std::tanh (upLeft[k] * gain) * makeup;
                upRight[k] = std::tanh (upRight[k] * gain) * makeup;
            }
        }

        oversampler->processSamplesDown (block);

        for (int i = 0; i < n; ++i)
        {
            if (tone.isSmoothing())
                toneCoefficient = coefficientFor (tone.getNextValue());

            const float m = mix.getNextValue();

            for (size_t c = 0; c < 2; ++c)
            {
                auto& state = toneState[c];
                state[0] += toneCoefficient * (channels[c][i] - state[0]);
                state[1] += toneCoefficient * (state[0] - state[1]);

                channels[c][i] = dry[c][(size_t) i] * (1.0f - m) + state[1] * m;
            }
        }
    }

    double sampleRate = 48000.0;
    int latency = 0;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<std::vector<float>, 2> dryRing;
    size_t ringIndex = 0;
    std::array<std::array<float, maxBlock>, 2> dry {};

    juce::SmoothedValue<float> driveDb { 12.0f }, tone { 0.5f }, mix { 1.0f };
    std::array<std::array<float, 2>, 2> toneState {};
    float toneCoefficient = 0.5f;
};

} // namespace scratchpad::dsp
