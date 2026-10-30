#pragma once

#include "dsp/Distortion.h"
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

    // M5: unison copies share the voice's filter and envelopes.
    int unison = 1;               // 1 .. 7 copies
    float unisonDetuneCents = 20.0f; // 0 .. 100: the outer copies sit this far either side
    float unisonSpread = 0.5f;    // 0 .. 1 stereo spread of the copies
    bool antiAlias = true;        // false: naive waveforms, no BLEP/BLAMP (a teaching switch)
    bool sync = false;            // osc 2 and 3 only: restart this cycle whenever oscillator 1 wraps
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
    bool shaperEnabled = false;
    dsp::ShapeMode shaperMode = dsp::ShapeMode::tanh;
    float shaperDriveDb = 12.0f;
    float shaperBias = 0.0f;
    float shaperMix = 1.0f;
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
    float drift = 0.0f;       // 0 .. 1: slow per-voice pitch/cutoff drift
};

/** Which oscillators oscillator 1 phase-modulates. The order matches `params::fmTargetNames()`. */
enum class FmTarget { osc2 = 0, osc3 = 1, both = 2 };

/** The four effects. Their saved order is a permutation of these; the integer values are stored. */
enum class EffectKind { distortion = 0, chorus = 1, delay = 2, reverb = 3 };
inline constexpr int numEffects = 4;

struct DistortionParams
{
    bool enabled = false;
    float driveDb = 12.0f;  // 0 .. 40
    float tone = 0.5f;      // 0 .. 1, dark to bright
    float mix = 1.0f;       // 0 .. 1
    dsp::ShapeMode mode = dsp::ShapeMode::tanh;
    float bias = 0.0f;      // -1 .. +1
};

struct ChorusParams
{
    bool enabled = false;
    float rateHz = 0.8f;    // 0.1 .. 8
    float depth = 0.5f;     // 0 .. 1
    float mix = 0.5f;       // 0 .. 1
};

struct DelayParams
{
    bool enabled = false;
    float timeMs = 350.0f;  // 1 .. 2000; the processor has already resolved tempo sync into this
    float feedback = 0.4f;  // 0 .. 0.95
    bool pingPong = false;
    float mix = 0.35f;      // 0 .. 1
};

struct ReverbParams
{
    bool enabled = false;
    float size = 0.5f;          // 0 .. 1: room size
    float decaySeconds = 2.0f;  // 0.2 .. 10: time for the tail to fall 60 dB
    float damping = 0.5f;       // 0 .. 1: how fast the tail loses treble
    float mix = 0.3f;           // 0 .. 1
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
    float ringLevelDb = -60.0f; // osc 1 x osc 2 as an extra source; at minLevelDb or below it is off
    float fmAmount = 0.0f;      // 0 .. 1: oscillator 1 phase-modulates `fmTarget`; 0 is off
    FmTarget fmTarget = FmTarget::osc2;
    DistortionParams distortion;
    ChorusParams chorus;
    DelayParams delay;
    ReverbParams reverb;
    std::array<EffectKind, numEffects> effectOrder { EffectKind::distortion, EffectKind::chorus, EffectKind::delay, EffectKind::reverb };
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
