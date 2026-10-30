#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/** Parameter IDs are a public contract with saved sessions and the copy file:
    never rename one after the first release. */
namespace scratchpad::params
{

/** Version hints: M1 parameters are 1, M2's are 2, M3's are 3. */
inline constexpr int versionHint = 1;
inline constexpr int versionHintM2 = 2;
inline constexpr int versionHintM3 = 3;
inline constexpr int versionHintM4 = 4;
inline constexpr int versionHintM5 = 5;

inline constexpr int numOscillators = 3;

/** IDs of one oscillator's parameters. `osc[0]` is oscillator 1. */
struct OscIds
{
    const char *enabled, *wave, *octave, *semitone, *fine, *pulseWidth, *level, *phaseReset;
    const char *unison, *unisonDetune, *unisonSpread, *antiAlias, *subLevel;
    const char* sync; // nullptr for oscillator 1: it is the sync master
};

inline constexpr std::array<OscIds, numOscillators> osc {{
    { "osc1.enabled", "osc1.wave", "osc1.octave", "osc1.semitone", "osc1.fine", "osc1.pulseWidth", "osc1.level", "osc1.phaseReset",
      "osc1.unison", "osc1.unisonDetune", "osc1.unisonSpread", "osc1.antiAlias", "osc1.subLevel", nullptr },
    { "osc2.enabled", "osc2.wave", "osc2.octave", "osc2.semitone", "osc2.fine", "osc2.pulseWidth", "osc2.level", "osc2.phaseReset",
      "osc2.unison", "osc2.unisonDetune", "osc2.unisonSpread", "osc2.antiAlias", "osc2.subLevel", "osc2.sync" },
    { "osc3.enabled", "osc3.wave", "osc3.octave", "osc3.semitone", "osc3.fine", "osc3.pulseWidth", "osc3.level", "osc3.phaseReset",
      "osc3.unison", "osc3.unisonDetune", "osc3.unisonSpread", "osc3.antiAlias", "osc3.subLevel", "osc3.sync" }
}};

// Oscillator 1 acts on the others. Ring modulation and FM are single patch-wide controls.
inline constexpr const char* ringLevel  = "ring.level";   // dB, minLevelDb = off
inline constexpr const char* fmAmount   = "fm.amount";    // percent
inline constexpr const char* fmTarget   = "fm.target";    // choice, see fmTargetNames()

inline constexpr int maxUnison = 7;
inline constexpr float unisonDetuneDefaultCents = 20.0f, unisonDetuneMaxCents = 100.0f, unisonSpreadDefaultPercent = 50.0f;

/** The four effects. Each has `<key>.enabled` (non-automatable presence), `<key>.bypass` and its own controls.
    Their saved order is the non-parameter tree property `effectOrder` (see ScratchpadProcessor). */
inline constexpr int numEffectKinds = 4;
inline constexpr std::array<const char*, numEffectKinds> effectKeys { "distortion", "chorus", "delay", "reverb" };
inline constexpr const char* effectOrderProperty = "effectOrder"; // string "0,1,2,3": a permutation of EffectKind, on the state tree

inline constexpr const char* distortionEnabled = "distortion.enabled";
inline constexpr const char* distortionDrive   = "distortion.drive";   // dB, 0 .. 40, default 12
inline constexpr const char* distortionTone    = "distortion.tone";    // percent, default 50
inline constexpr const char* distortionMix     = "distortion.mix";     // percent, default 100
inline constexpr const char* distortionMode    = "distortion.mode";
inline constexpr const char* distortionBias    = "distortion.bias";

inline constexpr const char* chorusEnabled = "chorus.enabled";
inline constexpr const char* chorusRate    = "chorus.rate";            // Hz, 0.1 .. 8, default 0.8
inline constexpr const char* chorusDepth   = "chorus.depth";           // percent, default 50
inline constexpr const char* chorusMix     = "chorus.mix";             // percent, default 50

inline constexpr const char* delayEnabled  = "delay.enabled";
inline constexpr const char* delayTime     = "delay.time";             // ms, 1 .. 2000 (log), default 350
inline constexpr const char* delaySync     = "delay.sync";             // bool: follow the tempo instead of `delay.time`
inline constexpr const char* delayDivision = "delay.division";         // choice, see tempoDivisionNames()
inline constexpr const char* delayFeedback = "delay.feedback";         // percent, 0 .. 95, default 40
inline constexpr const char* delayPingPong = "delay.pingPong";         // bool, default off
inline constexpr const char* delayMix      = "delay.mix";              // percent, default 35

inline constexpr const char* reverbEnabled = "reverb.enabled";
inline constexpr const char* reverbSize    = "reverb.size";            // percent, default 50
inline constexpr const char* reverbDecay   = "reverb.decay";           // seconds, 0.2 .. 10 (log), default 2
inline constexpr const char* reverbDamping = "reverb.damping";         // percent, default 50
inline constexpr const char* reverbMix     = "reverb.mix";             // percent, default 30

// Kept for the M1 call sites; these are `osc[0]`'s IDs.
inline constexpr const char* osc1Enabled = "osc1.enabled";
inline constexpr const char* osc1Wave    = "osc1.wave";
inline constexpr const char* osc1Level   = "osc1.level";

inline constexpr const char* noiseEnabled = "noise.enabled";
inline constexpr const char* noiseColour  = "noise.colour";
inline constexpr const char* noiseLevel   = "noise.level";

inline constexpr const char* filterEnabled    = "filter.enabled";
inline constexpr const char* filterMode       = "filter.mode";
inline constexpr const char* filterCutoff     = "filter.cutoff";
inline constexpr const char* filterResonance  = "filter.resonance";
inline constexpr const char* filterKeyTrack   = "filter.keyTracking";
inline constexpr const char* filterDrive      = "filter.drive";
inline constexpr const char* voiceShaperEnabled = "filter.shaper.enabled";
inline constexpr const char* voiceShaperMode = "filter.shaper.mode";
inline constexpr const char* voiceShaperDrive = "filter.shaper.drive";
inline constexpr const char* voiceShaperBias = "filter.shaper.bias";
inline constexpr const char* voiceShaperMix = "filter.shaper.mix";

/** IDs of an ADSR envelope's parameters. */
struct EnvIds
{
    const char *enabled, *attack, *decay, *sustain, *release;
};

inline constexpr EnvIds ampEnv    { "ampEnv.enabled", "ampEnv.attack", "ampEnv.decay", "ampEnv.sustain", "ampEnv.release" };
inline constexpr EnvIds filterEnv { "filterEnv.enabled", "filterEnv.attack", "filterEnv.decay", "filterEnv.sustain", "filterEnv.release" };

inline constexpr const char* ampEnvVelocity    = "ampEnv.velocitySens";
inline constexpr const char* filterEnvAmount   = "filterEnv.amount";

/** Modules that can be bypassed, by the prefix of their parameter IDs: `<key>.bypass`.
    The first four are sources and can also be soloed: `<key>.solo`. */
inline constexpr int numBypassable = 13;
inline constexpr int numSoloable = 4;
inline constexpr std::array<const char*, numBypassable> bypassKeys {
    "osc1", "osc2", "osc3", "noise", "filter", "ampEnv", "filterEnv", "lfo1", "lfo2",
    "distortion", "chorus", "delay", "reverb"
};

inline constexpr int numLfos = 2;

/** IDs of one LFO's parameters. `lfo[0]` is LFO 1. */
struct LfoIds
{
    const char *enabled, *shape, *rate, *retrigger, *fade;
    const char *sync, *division; // bool, and a choice from tempoDivisionNames(): the rate follows the tempo when sync is on
};

inline constexpr std::array<LfoIds, numLfos> lfo {{
    { "lfo1.enabled", "lfo1.shape", "lfo1.rate", "lfo1.retrigger", "lfo1.fade", "lfo1.sync", "lfo1.division" },
    { "lfo2.enabled", "lfo2.shape", "lfo2.rate", "lfo2.retrigger", "lfo2.fade", "lfo2.sync", "lfo2.division" }
}};

/** The eight modulation connection slots. A slot is unused while its source or destination is "None". */
inline constexpr int numModSlots = 8;

struct ModIds
{
    const char *source, *dest, *amount;
};

inline constexpr std::array<ModIds, numModSlots> mod {{
    { "mod1.source", "mod1.dest", "mod1.amount" }, { "mod2.source", "mod2.dest", "mod2.amount" },
    { "mod3.source", "mod3.dest", "mod3.amount" }, { "mod4.source", "mod4.dest", "mod4.amount" },
    { "mod5.source", "mod5.dest", "mod5.amount" }, { "mod6.source", "mod6.dest", "mod6.amount" },
    { "mod7.source", "mod7.dest", "mod7.amount" }, { "mod8.source", "mod8.dest", "mod8.amount" }
}};

inline constexpr const char* voicingMode      = "voicing.mode";
inline constexpr const char* voicingVoices    = "voicing.voices";
inline constexpr const char* voicingGlide     = "voicing.glide";
inline constexpr const char* voicingBendRange = "voicing.bendRange";
inline constexpr const char* voicingDrift     = "voicing.drift";
inline constexpr const char* outVolume = "output.volume";

// Ranges and defaults, in the units the parameter holds.
inline constexpr float oscLevelMinDb = -60.0f;
inline constexpr float oscLevelMaxDb = 0.0f;
inline constexpr float oscLevelDefaultDb = -6.0f;
inline constexpr float noiseLevelDefaultDb = -18.0f;

inline constexpr float ringLevelDefaultDb = -60.0f; // the bottom of the range is off

inline constexpr float distortionDriveMaxDb = 40.0f, distortionDriveDefaultDb = 12.0f;
inline constexpr float chorusRateMinHz = 0.1f, chorusRateMaxHz = 8.0f, chorusRateDefaultHz = 0.8f;
inline constexpr float delayTimeMinMs = 1.0f, delayTimeMaxMs = 2000.0f, delayTimeDefaultMs = 350.0f;
inline constexpr float reverbDecayMinSeconds = 0.2f, reverbDecayMaxSeconds = 10.0f, reverbDecayDefaultSeconds = 2.0f;
inline constexpr float newOscillatorFineCents = 7.0f; // a new Osc 2 or 3 starts detuned so it is audible at once

inline constexpr float volumeMinDb = -60.0f;
inline constexpr float volumeMaxDb = 6.0f;
inline constexpr float volumeDefaultDb = 0.0f;

inline constexpr float cutoffMinHz = 20.0f, cutoffMaxHz = 20000.0f, cutoffDefaultHz = 2000.0f;
inline constexpr float driveMaxDb = 24.0f;

inline constexpr float envTimeMinSeconds = 0.001f;
inline constexpr float envTimeMaxSeconds = 10.0f;
inline constexpr float releaseMaxSeconds = 20.0f;

inline constexpr float lfoRateMinHz = 0.01f, lfoRateMaxHz = 50.0f, lfoRateDefaultHz = 2.0f;
inline constexpr float lfoFadeMaxSeconds = 5.0f;

/** The amount a new connection starts at, in percent. */
inline constexpr float modAmountDefault = 50.0f;

inline constexpr float glideMaxSeconds = 2.0f;
inline constexpr int maxVoices = 16;
inline constexpr int defaultVoices = 8;
inline constexpr int bendRangeMin = 1, bendRangeMax = 24, bendRangeDefault = 2;

/** Choice order matches `scratchpad::dsp::Wave`. */
juce::StringArray waveNames();

/** Choice order matches `scratchpad::dsp::FilterMode`. */
juce::StringArray filterModeNames();

/** Poly, mono, legato: matches `scratchpad::engine::VoiceMode`. */
juce::StringArray voiceModeNames();

juce::StringArray noiseColourNames();

/** Choice order matches `scratchpad::dsp::LfoShape`. */
juce::StringArray lfoShapeNames();

/** Choice order matches `scratchpad::engine::ModSource`; index 0 is "None". */
juce::StringArray modSourceNames();

/** Choice order matches `scratchpad::engine::ModDest`; index 0 is "None". */
juce::StringArray modDestNames();

/** Order matches `scratchpad::engine::FmTarget`: Osc 2, Osc 3, Both. */
juce::StringArray fmTargetNames();

/** "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars": lengths 1/8, 1/4, 1/2, 1, 2, 4, 8, 16 beats
    (a beat is a quarter note). Shared by the LFOs and the delay. */
juce::StringArray tempoDivisionNames();
inline constexpr std::array<float, 8> tempoDivisionBeats { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
inline constexpr int tempoDivisionDefault = 3; // 1/4

/** Formats a level in dB for display: "-6.0 dB", or "-inf" at the bottom of the range. */
juce::String levelText (float db, float minDb);

/** "5 ms", "1.20 s": time in seconds, in the units a person would say. */
juce::String timeText (float seconds);

/** "2.0 kHz", "440 Hz". */
juce::String frequencyText (float hz);

/** Tempo sync. A division's length is `tempoDivisionBeats[index]` quarter notes. The LFO rate in Hz is
    bpm / 60 / beats (limited to the LFO's own range); the delay time in ms is 60000 / bpm * beats (limited to
    1..2000 ms). A tempo that is zero, negative or not a number (a host with no transport) counts as 120. */
float lfoSyncRateHz (double bpm, int divisionIndex) noexcept;
float delaySyncMs (double bpm, int divisionIndex) noexcept;
inline constexpr double defaultBpm = 120.0;

/** "50%" for a value already in percent. */
juce::String percentText (float percent);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace scratchpad::params
