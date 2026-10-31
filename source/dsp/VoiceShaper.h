#pragma once

#include "dsp/Distortion.h" // ShapeMode
#include "dsp/FastTanh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace scratchpad::dsp
{

/** The per-voice waveshaper: tanh, hard clip or wavefolder at twice the sample rate, one sample at a time.

    It is the cheap cousin of `Distortion`: JUCE's oversampler is built for blocks and costs a great deal
    on one-sample calls, so this uses a polyphase pair of all-pass chains (the classic two-path IIR half-band,
    coefficients 0.0799 / 0.5454 and 0.2838 / 0.8344) for both the up- and the down-sampling. The pass-band is
    flat to about 0.4 of the base Nyquist, the stop-band is better than 68 dB, and the delay is a few samples
    that fall mostly at high frequencies, so the dry path is not delayed and a mix of less than 100% does not
    comb. There is no tone filter (the shaper is "tone open"). A 7 Hz DC blocker follows, which matters
    for bias.

    Drive and bias glide with a one-pole (about 4 ms) so a knob turn does not zipper. The output is scaled by
    `gain^-0.25` like `Distortion`. */
class VoiceShaper
{
public:
    void prepare (double sr) noexcept
    {
        dcPole = 1.0f - 2.0f * std::numbers::pi_v<float> * 7.0f / (float) sr;
        glide = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c = Channel {};

        driveDb = driveTarget;
        bias = biasTarget;
        updateGain();
    }

    /** Drive in dB (0 .. 40) and bias (-1 .. 1) the shaper is moving towards. */
    void setTarget (float newDriveDb, float newBias, ShapeMode newMode) noexcept
    {
        driveTarget = newDriveDb;
        biasTarget = newBias;
        mode = newMode;
    }

    /** One sample, in place. `stereo` false leaves `r` alone (the voice then copies `l`). */
    void process (float& l, float& r, bool stereo) noexcept
    {
        driveDb += (driveTarget - driveDb) * glide;
        bias += (biasTarget - bias) * glide;

        if (std::abs (driveDb - shapedDriveDb) > 0.005f)
            updateGain();

        l = processChannel (channels[0], l);

        if (stereo)
            r = processChannel (channels[1], r);
    }

private:
    struct Allpass
    {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;

        float tick (float x) noexcept
        {
            // y[n] = a * (x[n] - y[n-1]) + x[n-1]
            const float y = a * (x - y1) + x1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    struct Chain
    {
        Chain() = default;
        Chain (float a0, float a1) noexcept { stage[0].a = a0; stage[1].a = a1; }

        std::array<Allpass, 2> stage {};

        float tick (float x) noexcept
        {
            return stage[1].tick (stage[0].tick (x));
        }
    };

    struct Channel
    {
        // Up: even output = path 0, odd output = path 1. Down: out = 0.5 * (path 0 (even) + path 1 (previous odd)).
        std::array<Chain, 2> up { Chain (0.07986642623635751f, 0.5453536510711322f), Chain (0.28382934487410993f, 0.8344118914807379f) };
        std::array<Chain, 2> down { Chain (0.07986642623635751f, 0.5453536510711322f), Chain (0.28382934487410993f, 0.8344118914807379f) };
        float lastOdd = 0.0f, dcIn = 0.0f, dcOut = 0.0f;
    };

    float processChannel (Channel& c, float x) noexcept
    {
        const float u0 = shape (c.up[0].tick (x));
        const float u1 = shape (c.up[1].tick (x));

        const float even = c.down[0].tick (u0);
        const float odd = c.down[1].tick (c.lastOdd);
        c.lastOdd = u1;

        const float y = 0.5f * (even + odd);

        const float blocked = y - c.dcIn + dcPole * c.dcOut;
        c.dcIn = y;
        c.dcOut = blocked;
        return blocked;
    }

    float shape (float input) const noexcept
    {
        const float x = input * shapeGain + bias;

        switch (mode)
        {
            case ShapeMode::tanh: return fastTanh (x) * shapeMakeup;
            case ShapeMode::hardClip: return std::clamp (x, -1.0f, 1.0f) * shapeMakeup;
            case ShapeMode::wavefolder:
            {
                float folded = std::fmod (x + 1.0f, 4.0f);
                if (folded < 0.0f)
                    folded += 4.0f;
                folded = folded < 2.0f ? folded : 4.0f - folded;
                return (folded - 1.0f) * shapeMakeup;
            }
        }

        return input;
    }

    void updateGain() noexcept
    {
        shapedDriveDb = driveDb;
        shapeGain = std::exp (driveDb * 0.11512925f);
        shapeMakeup = std::exp (-0.25f * driveDb * 0.11512925f);
    }

    std::array<Channel, 2> channels {};
    ShapeMode mode = ShapeMode::tanh;
    float driveTarget = 12.0f, driveDb = 12.0f, biasTarget = 0.0f, bias = 0.0f;
    float shapedDriveDb = 1.0e9f, shapeGain = 1.0f, shapeMakeup = 1.0f;
    float glide = 0.005f, dcPole = 0.999f;
};

} // namespace scratchpad::dsp
