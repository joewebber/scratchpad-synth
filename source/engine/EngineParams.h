#pragma once

#include "dsp/Lfo.h"
#include "dsp/MultimodeFilter.h"
#include "dsp/Oscillator.h"
#include "engine/Modulation.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace scratchpad::engine
{

/** Plain values the engine needs from the parameter tree. The processor reads
    its cached atomics once per block and hands this over, so the engine never
    touches JUCE parameter objects. Units are the engine's, not the UI's:
    seconds, 0..1 fractions, cents, dB. */

struct OscParams
{
    bool enabled = false;
    dsp::Wave wave = dsp::Wave::saw;
    int octave = 0;           // -2 .. +2
    int semitone = 0;         // -12 .. +12
    float fineCents = 0.0f;   // -100 .. +100
    float pulseWidth = 0.5f;  // 0.05 .. 0.95
    float levelDb = -6.0f;
    bool phaseReset = true;   // restart the cycle on every note
};

struct NoiseParams
{
    bool enabled = false;
    bool pink = false;
    float levelDb = -18.0f;
};

struct FilterParams
{
    bool enabled = false;
    dsp::FilterMode mode = dsp::FilterMode::lp24;
    float cutoffHz = 2000.0f;
    float resonance = 0.2f;   // 0 .. 1
    float keyTracking = 0.0f; // 0 .. 1; 1 moves the cutoff one octave per octave played
    float driveDb = 0.0f;     // 0 .. 24
};

struct EnvelopeParams
{
    bool enabled = false;
    float attack = 0.005f;    // seconds
    float decay = 0.3f;       // seconds
    float sustain = 0.8f;     // 0 .. 1
    float release = 0.2f;     // seconds
};

struct AmpEnvelopeParams : EnvelopeParams
{
    float velocitySensitivity = 0.5f; // 0 .. 1
};

struct FilterEnvelopeParams : EnvelopeParams
{
    float amount = 0.5f;      // -1 .. +1; 1 is +6 octaves of cutoff at the envelope's peak
};

struct LfoParams
{
    bool enabled = false;
    dsp::LfoShape shape = dsp::LfoShape::sine;
    float rateHz = 2.0f;      // 0.01 .. 50
    bool retrigger = false;   // restart the cycle on every note, per voice
    float fadeSeconds = 0.0f; // 0 .. 5: the LFO's depth rises over this long after each note starts
};

enum class VoiceMode { poly = 0, mono = 1, legato = 2 };

struct VoicingParams
{
    VoiceMode mode = VoiceMode::poly;
    int voices = 8;           // 1 .. 16, poly only
    float glideSeconds = 0.0f;
    float bendRange = 2.0f;   // semitones either way
};

struct EngineParams
{
    std::array<OscParams, 3> osc;
    NoiseParams noise;
    FilterParams filter;
    AmpEnvelopeParams ampEnv;
    FilterEnvelopeParams filterEnv;
    VoicingParams voicing;
    std::array<LfoParams, 2> lfo;
    ModConnections mod;
    float outputVolumeDb = 0.0f;
};

/** Below this a level control means "off" (shown as -inf in the UI). */
inline constexpr float minLevelDb = -60.0f;

inline float dbToGain (float db) noexcept
{
    return db <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

/** Octaves of cutoff movement at an envelope amount of +1 (the brief's "+-6 octaves at 100%"). */
inline constexpr float filterEnvOctaves = 6.0f;

} // namespace scratchpad::engine
