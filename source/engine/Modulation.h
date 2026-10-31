#pragma once

#include "dsp/Lfo.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace scratchpad::engine
{

/** What can drive a modulation connection. The integer values are saved in
    sessions (`modN.source` is a choice index): append, never reorder. */
enum class ModSource : int
{
    none = 0,
    lfo1,
    lfo2,
    ampEnv,
    filterEnv,
    velocity,
    modWheel,
    aftertouch,
    key
};

inline constexpr int numModSources = 9;

/** What a connection can move. Saved as `modN.dest` choice indices: append, never reorder. */
enum class ModDest : int
{
    none = 0,
    pitchAll,
    pitch1, pitch2, pitch3,
    pulseWidth1, pulseWidth2, pulseWidth3,
    level1, level2, level3,
    noiseLevel,
    cutoff,
    resonance,
    ampLevel,
    pan,
    lfo1Rate,
    lfo2Rate,
    filterDrive,
    fmAmount,
    shaperDrive,
    unisonDetune1,
    unisonDetune2,
    unisonDetune3,
    sync2,
    sync3,
    pitchWide
};

inline constexpr int numModDests = 27;
inline constexpr int numModSlots = 8;

struct ModConnection
{
    ModSource source = ModSource::none;
    ModDest dest = ModDest::none;
    float amount = 0.5f; // -1 .. +1

    bool active() const noexcept { return source != ModSource::none && dest != ModDest::none; }
};

using ModConnections = std::array<ModConnection, numModSlots>;

/** The current value of every source, indexed by `ModSource`. LFOs are
    bipolar (-1 .. +1); envelopes, velocity, mod wheel and aftertouch are 0 .. 1;
    key is the note's distance from middle C, (note - 60) / 60, limited to +-1. */
using ModSourceValues = std::array<float, numModSources>;

/** Sum over the connections of `amount * source` for each destination. */
using ModSums = std::array<float, numModDests>;

constexpr size_t index (ModSource s) noexcept { return (size_t) s; }
constexpr size_t index (ModDest d) noexcept { return (size_t) d; }

/** What +100% means for each kind of destination. A sum of 1.0 moves the destination this far;
    -1.0 moves it as far the other way. */
inline constexpr float modPitchSemitones = 2.0f;      // pitch, 100% = 2 semitones (saved patches rely on it)
inline constexpr float modPitchWideSemitones = 24.0f; // "Pitch (wide)", two octaves at 100%
inline constexpr float modCutoffOctaves = 6.0f;    // filter cutoff, the same scale as the filter envelope's amount
inline constexpr float modResonance = 1.0f;        // the whole 0 .. 1 range
inline constexpr float modPulseWidth = 0.45f;      // pulse width, as a fraction of the cycle
inline constexpr float modPan = 1.0f;              // hard left to hard right
inline constexpr float modLfoRateOctaves = 3.0f;   // LFO rate
inline constexpr float modFilterDriveDb = 24.0f;
inline constexpr float modFmAmount = 1.0f;
inline constexpr float modShaperDriveDb = 40.0f;
inline constexpr float modUnisonDetuneCents = 100.0f;

/** Level destinations scale the knob's gain by `1 + sum`, so -100% is silence and +100% doubles it (+6 dB). */
inline constexpr float modOscLevelMaxFactor = 2.0f;
/** The amp level is already full, so modulation can only pull it down. */
inline constexpr float modAmpLevelMaxFactor = 1.0f;

inline float levelFactor (float sum, float maxFactor) noexcept
{
    return std::clamp (1.0f + sum, 0.0f, maxFactor);
}

constexpr ModDest pitchDest (size_t osc) noexcept       { return (ModDest) ((int) ModDest::pitch1 + (int) osc); }
constexpr ModDest pulseWidthDest (size_t osc) noexcept  { return (ModDest) ((int) ModDest::pulseWidth1 + (int) osc); }
constexpr ModDest levelDest (size_t osc) noexcept       { return (ModDest) ((int) ModDest::level1 + (int) osc); }
constexpr ModDest lfoRateDest (size_t lfo) noexcept     { return (ModDest) ((int) ModDest::lfo1Rate + (int) lfo); }
constexpr ModSource lfoSource (size_t lfo) noexcept     { return (ModSource) ((int) ModSource::lfo1 + (int) lfo); }

/** Adds up every active connection. A source with value 0 contributes nothing,
    so a source whose module is absent is simply left at 0 in `sources`. */
inline ModSums accumulate (const ModConnections& connections, const ModSourceValues& sources) noexcept
{
    ModSums sums {};

    for (const auto& c : connections)
        if (c.active())
            sums[index (c.dest)] += c.amount * sources[index (c.source)];

    return sums;
}

/** Rate of an LFO whose rate knob says `baseHz`, moved by `sum` (a `lfoRateDest` sum). */
inline float modulatedLfoRate (float baseHz, float sum) noexcept
{
    return std::clamp (baseHz * std::exp2 (sum * modLfoRateOctaves), (float) dsp::lfoMinHz, (float) dsp::lfoMaxHz);
}

} // namespace scratchpad::engine
