#pragma once

#include "engine/EngineParams.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace scratchpad::engine
{

/** A value that moves in a straight line from `start` to `end` across one
    sub-block; `t` is 0 at the start and 1 at the last sample. */
struct Ramp
{
    float start = 0.0f;
    float end = 0.0f;

    float at (float t) const noexcept { return start + (end - start) * t; }
};

/** The smoothed, patch-wide values every voice mixes with, for one sub-block.
    The engine owns the smoothers (so all voices follow the same curve) and
    hands each voice the value at the start and end of the sub-block; the voice
    interpolates in between. */
struct BlockLevels
{
    std::array<Ramp, 3> osc;  // linear gain, already 0 for a disabled oscillator
    Ramp noise;               // linear gain, 0 when disabled
    Ramp preview;             // linear gain of the wave-picker preview voice (osc 1's level)
    Ramp cutoffLog2;          // log2 of the filter cutoff in Hz, before key tracking and envelope
    Ramp resonance;           // 0 .. 1
    Ramp driveDb;             // 0 .. 24
    Ramp filterEnvAmount;     // -1 .. +1, already 0 when the filter envelope is disabled

    /** What the levels settle to for `p`: every ramp is flat. */
    static BlockLevels steady (const EngineParams& p) noexcept
    {
        BlockLevels b;

        for (size_t i = 0; i < b.osc.size(); ++i)
        {
            const float g = p.osc[i].enabled ? dbToGain (p.osc[i].levelDb) : 0.0f;
            b.osc[i] = { g, g };
        }

        const float noiseGain = p.noise.enabled ? dbToGain (p.noise.levelDb) : 0.0f;
        const float previewGain = dbToGain (p.osc[0].levelDb);
        const float cutoff = std::log2 (std::max (p.filter.cutoffHz, 1.0f));
        const float amount = p.filterEnv.enabled ? p.filterEnv.amount : 0.0f;

        b.noise = { noiseGain, noiseGain };
        b.preview = { previewGain, previewGain };
        b.cutoffLog2 = { cutoff, cutoff };
        b.resonance = { p.filter.resonance, p.filter.resonance };
        b.driveDb = { p.filter.driveDb, p.filter.driveDb };
        b.filterEnvAmount = { amount, amount };
        return b;
    }
};

} // namespace scratchpad::engine
