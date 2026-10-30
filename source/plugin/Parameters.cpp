#include "plugin/Parameters.h"

#include <cmath>

namespace scratchpad::params
{

juce::StringArray waveNames()        { return { "Sine", "Triangle", "Saw", "Pulse" }; }
juce::StringArray filterModeNames()  { return { "LP 24", "LP 12", "HP 12", "BP 12" }; }
juce::StringArray voiceModeNames()   { return { "Poly", "Mono", "Legato" }; }
juce::StringArray noiseColourNames() { return { "White", "Pink" }; }
juce::StringArray lfoShapeNames()    { return { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold" }; }

namespace
{
    double sanitiseBpm (double bpm) noexcept
    {
        return std::isfinite (bpm) && bpm > 0.0 ? bpm : defaultBpm;
    }
}

juce::StringArray modSourceNames()
{
    return { "None", "LFO 1", "LFO 2", "Amp Envelope", "Filter Envelope", "Velocity", "Mod wheel", "Aftertouch", "Key" };
}

juce::StringArray modDestNames()
{
    return { "None", "Pitch (all oscillators)", "Osc 1 pitch", "Osc 2 pitch", "Osc 3 pitch",
             "Osc 1 pulse width", "Osc 2 pulse width", "Osc 3 pulse width",
             "Osc 1 level", "Osc 2 level", "Osc 3 level", "Noise level",
             "Filter cutoff", "Filter resonance", "Amp level", "Pan", "LFO 1 rate", "LFO 2 rate" };
}

juce::StringArray fmTargetNames()      { return { "Osc 2", "Osc 3", "Both" }; }

juce::StringArray tempoDivisionNames() { return { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }; }

float lfoSyncRateHz (double bpm, int divisionIndex) noexcept
{
    const double beats = (double) tempoDivisionBeats[(size_t) juce::jlimit (0, (int) tempoDivisionBeats.size() - 1, divisionIndex)];
    return juce::jlimit (lfoRateMinHz, lfoRateMaxHz, (float) (sanitiseBpm (bpm) / 60.0 / beats));
}

float delaySyncMs (double bpm, int divisionIndex) noexcept
{
    const double beats = (double) tempoDivisionBeats[(size_t) juce::jlimit (0, (int) tempoDivisionBeats.size() - 1, divisionIndex)];
    return juce::jlimit (delayTimeMinMs, delayTimeMaxMs, (float) (60000.0 / sanitiseBpm (bpm) * beats));
}

juce::String levelText (float db, float minDb)
{
    if (db <= minDb)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")) + " dB"; // "−∞ dB"

    return juce::String (db, 1) + " dB";
}

juce::String timeText (float seconds)
{
    if (seconds < 0.0095f)
        return juce::String (seconds * 1000.0f, 1) + " ms";

    if (seconds < 1.0f)
        return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";

    return juce::String (seconds, seconds < 10.0f ? 2 : 1) + " s";
}

juce::String frequencyText (float hz)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";

    return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
}

juce::String percentText (float percent)
{
    return juce::String (juce::roundToInt (percent)) + "%";
}

namespace
{
    using juce::AudioParameterBool;
    using juce::AudioParameterBoolAttributes;
    using juce::AudioParameterChoice;
    using juce::AudioParameterChoiceAttributes;
    using juce::AudioParameterFloat;
    using juce::AudioParameterFloatAttributes;
    using juce::AudioParameterInt;
    using juce::AudioParameterIntAttributes;
    using juce::ParameterID;

    /** Logarithmic taper between two positive limits. The maths is done in double: a float `pow` and `log` do not
        invert each other closely enough (about 5e-7 relative at the ends of a 1000:1 range), so a value that went
        into the state tree and out again came back a few ulps different, and the APVTS then rewrote the tree
        (through the undo manager) when it flushed, which cleared Redo. In double the round trip lands back on the same
        float, so `convertTo0to1 (convertFrom0to1 (n)) == n`; `UndoFlushTests` checks every parameter. */
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float start, float end, float t)
                 { return (float) ((double) start * std::pow ((double) end / (double) start, (double) t)); },
                 [] (float start, float end, float v)
                 { return (float) (std::log ((double) v / (double) start) / std::log ((double) end / (double) start)); },
                 [] (float start, float end, float v) { return juce::jlimit (start, end, v); } };
    }

    void addPresence (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                      const juce::String& name, int hint, bool defaultOn = false)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, hint }, name, defaultOn,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));
    }

    void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                    const juce::String& name, const juce::StringArray& choices, int defaultIndex, int hint, bool automatable = false)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, hint }, name, choices, defaultIndex,
                                                            AudioParameterChoiceAttributes().withAutomatable (automatable)));
    }

    void addLevel (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                   const juce::String& name, float minDb, float maxDb, float defaultDb, int hint)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, hint }, name, juce::NormalisableRange<float> (minDb, maxDb, 0.1f), defaultDb,
            AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([minDb] (float v, int) { return levelText (v, minDb); })));
    }

    void addTime (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                  const juce::String& name, float maxSeconds, float defaultSeconds)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, versionHintM2 }, name, logRange (envTimeMinSeconds, maxSeconds), defaultSeconds,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return timeText (v); })));
    }

    void addPercent (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                     const juce::String& name, float lo, float hi, float defaultValue, int hint = versionHintM2)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, hint }, name, juce::NormalisableRange<float> (lo, hi, 1.0f), defaultValue,
            AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return percentText (v); })));
    }

    void addToggle (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id,
                    const juce::String& name, bool defaultOn, bool automatable)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, versionHintM5 }, name, defaultOn,
                                                          AudioParameterBoolAttributes().withAutomatable (automatable)));
    }

    void addValue (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const juce::String& name,
                   juce::NormalisableRange<float> range, float defaultValue, const juce::String& label,
                   std::function<juce::String (float)> text)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, versionHintM5 }, name, range, defaultValue,
            AudioParameterFloatAttributes()
                .withLabel (label)
                .withStringFromValueFunction ([text] (float v, int) { return text (v); })));
    }

    void addEnvelope (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const EnvIds& ids,
                      const juce::String& name, float attack, float decay, float sustainPercent, float release)
    {
        addPresence (layout, ids.enabled, name + " present", versionHintM2);
        addTime (layout, ids.attack, name + " attack", envTimeMaxSeconds, attack);
        addTime (layout, ids.decay, name + " decay", envTimeMaxSeconds, decay);
        addPercent (layout, ids.sustain, name + " sustain", 0.0f, 100.0f, sustainPercent);
        addTime (layout, ids.release, name + " release", releaseMaxSeconds, release);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Oscillators. Oscillator 1's IDs and ranges are unchanged from the first release.
    for (int i = 0; i < numOscillators; ++i)
    {
        const auto& ids = osc[(size_t) i];
        const auto n = juce::String (i + 1);
        const auto name = "Oscillator " + n;
        const int hint = i == 0 ? versionHint : versionHintM2;

        addPresence (layout, ids.enabled, name + " present", hint, i == 0); // oscillator 1 is always part of the patch
        addChoice (layout, ids.wave, name + " wave", waveNames(), 2, hint);

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.octave, versionHintM2 }, name + " octave", -2, 2, 0,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v > 0 ? "+" + juce::String (v) : juce::String (v); })));

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.semitone, versionHintM2 }, name + " semitone", -12, 12, 0,
            AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return v > 0 ? "+" + juce::String (v) : juce::String (v); })));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.fine, versionHintM2 }, name + " fine", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
            AudioParameterFloatAttributes()
                .withLabel ("cents")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " c"; })));

        addPercent (layout, ids.pulseWidth, name + " pulse width", 5.0f, 95.0f, 50.0f);

        addLevel (layout, ids.level, name + " level", oscLevelMinDb, oscLevelMaxDb, oscLevelDefaultDb, hint);

        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids.phaseReset, versionHintM2 },
                                                          name + " phase reset on note", true,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { ids.unison, versionHintM5 }, name + " unison voices", 1, maxUnison, 1));
        addValue (layout, ids.unisonDetune, name + " unison detune", juce::NormalisableRange<float> (0.0f, unisonDetuneMaxCents, 1.0f),
                  unisonDetuneDefaultCents, "cents", [] (float v) { return juce::String (juce::roundToInt (v)) + " c"; });
        addPercent (layout, ids.unisonSpread, name + " unison spread", 0.0f, 100.0f, unisonSpreadDefaultPercent, versionHintM5);
        addToggle (layout, ids.antiAlias, name + " anti-aliasing", true, true);

        if (ids.sync != nullptr)
            addToggle (layout, ids.sync, name + " sync to oscillator 1", false, true);
    }

    // Oscillator 1 acts on the others.
    addLevel (layout, ringLevel, "Ring modulation level", oscLevelMinDb, oscLevelMaxDb, ringLevelDefaultDb, versionHintM5);
    addPercent (layout, fmAmount, "FM amount", 0.0f, 100.0f, 0.0f, versionHintM5);
    addChoice (layout, fmTarget, "FM target", fmTargetNames(), 0, versionHintM5, true);

    // Noise.
    addPresence (layout, noiseEnabled, "Noise present", versionHintM2);
    addChoice (layout, noiseColour, "Noise colour", noiseColourNames(), 0, versionHintM2);
    addLevel (layout, noiseLevel, "Noise level", oscLevelMinDb, oscLevelMaxDb, noiseLevelDefaultDb, versionHintM2);

    // Filter.
    addPresence (layout, filterEnabled, "Filter present", versionHintM2);
    addChoice (layout, filterMode, "Filter mode", filterModeNames(), 0, versionHintM2);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { filterCutoff, versionHintM2 }, "Filter cutoff", logRange (cutoffMinHz, cutoffMaxHz), cutoffDefaultHz,
        AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return frequencyText (v); })));
    addPercent (layout, filterResonance, "Filter resonance", 0.0f, 100.0f, 20.0f);
    addPercent (layout, filterKeyTrack, "Filter key tracking", 0.0f, 100.0f, 0.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { filterDrive, versionHintM2 }, "Filter drive", juce::NormalisableRange<float> (0.0f, driveMaxDb, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));

    // Envelopes.
    addEnvelope (layout, ampEnv, "Amp envelope", 0.005f, 0.3f, 80.0f, 0.2f);
    addPercent (layout, ampEnvVelocity, "Amp envelope velocity sensitivity", 0.0f, 100.0f, 50.0f);

    addEnvelope (layout, filterEnv, "Filter envelope", 0.005f, 0.5f, 0.0f, 0.3f);
    addPercent (layout, filterEnvAmount, "Filter envelope amount", -100.0f, 100.0f, 50.0f);

    // LFOs.
    for (int i = 0; i < numLfos; ++i)
    {
        const auto& ids = lfo[(size_t) i];
        const auto name = "LFO " + juce::String (i + 1);

        addPresence (layout, ids.enabled, name + " present", versionHintM3);
        addChoice (layout, ids.shape, name + " shape", lfoShapeNames(), 0, versionHintM3);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.rate, versionHintM3 }, name + " rate", logRange (lfoRateMinHz, lfoRateMaxHz), lfoRateDefaultHz,
            AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " Hz"; })));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids.retrigger, versionHintM3 }, name + " retrigger on note", false,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.fade, versionHintM3 }, name + " fade-in", juce::NormalisableRange<float> (0.0f, lfoFadeMaxSeconds, 0.01f, 0.5f), 0.0f,
            AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return v < 0.005f ? juce::String ("Off") : timeText (v); })));
        addToggle (layout, ids.sync, name + " tempo sync", false, true);
        addChoice (layout, ids.division, name + " division", tempoDivisionNames(), tempoDivisionDefault, versionHintM5, true);
    }

    // Effects: presence and bypass are structural; everything else is a knob. Their order is a tree property, not a parameter.
    addPresence (layout, distortionEnabled, "Distortion present", versionHintM5);
    addValue (layout, distortionDrive, "Distortion drive", juce::NormalisableRange<float> (0.0f, distortionDriveMaxDb, 0.1f), distortionDriveDefaultDb,
              "dB", [] (float v) { return juce::String (v, 1) + " dB"; });
    addPercent (layout, distortionTone, "Distortion tone", 0.0f, 100.0f, 50.0f, versionHintM5);
    addPercent (layout, distortionMix, "Distortion mix", 0.0f, 100.0f, 100.0f, versionHintM5);

    addPresence (layout, chorusEnabled, "Chorus present", versionHintM5);
    addValue (layout, chorusRate, "Chorus rate", logRange (chorusRateMinHz, chorusRateMaxHz), chorusRateDefaultHz,
              "Hz", [] (float v) { return juce::String (v, 2) + " Hz"; });
    addPercent (layout, chorusDepth, "Chorus depth", 0.0f, 100.0f, 50.0f, versionHintM5);
    addPercent (layout, chorusMix, "Chorus mix", 0.0f, 100.0f, 50.0f, versionHintM5);

    addPresence (layout, delayEnabled, "Delay present", versionHintM5);
    addValue (layout, delayTime, "Delay time", logRange (delayTimeMinMs, delayTimeMaxMs), delayTimeDefaultMs,
              "ms", [] (float v) { return timeText (v * 0.001f); });
    addToggle (layout, delaySync, "Delay tempo sync", false, true);
    addChoice (layout, delayDivision, "Delay division", tempoDivisionNames(), tempoDivisionDefault, versionHintM5, true);
    addPercent (layout, delayFeedback, "Delay feedback", 0.0f, 95.0f, 40.0f, versionHintM5);
    addToggle (layout, delayPingPong, "Delay ping-pong", false, true);
    addPercent (layout, delayMix, "Delay mix", 0.0f, 100.0f, 35.0f, versionHintM5);

    addPresence (layout, reverbEnabled, "Reverb present", versionHintM5);
    addPercent (layout, reverbSize, "Reverb size", 0.0f, 100.0f, 50.0f, versionHintM5);
    addValue (layout, reverbDecay, "Reverb decay", logRange (reverbDecayMinSeconds, reverbDecayMaxSeconds), reverbDecayDefaultSeconds,
              "s", [] (float v) { return timeText (v); });
    addPercent (layout, reverbDamping, "Reverb damping", 0.0f, 100.0f, 50.0f, versionHintM5);
    addPercent (layout, reverbMix, "Reverb mix", 0.0f, 100.0f, 30.0f, versionHintM5);

    // Modulation connections: eight fixed slots.
    for (int i = 0; i < numModSlots; ++i)
    {
        const auto& ids = mod[(size_t) i];
        const auto name = "Modulation " + juce::String (i + 1);

        addChoice (layout, ids.source, name + " source", modSourceNames(), 0, versionHintM3);
        addChoice (layout, ids.dest, name + " destination", modDestNames(), 0, versionHintM3);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ids.amount, versionHintM3 }, name + " amount", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), modAmountDefault,
            AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + percentText (v); })));
    }

    // Bypass for every module, solo for the sources. Neither is automatable: they are a way of listening, not of playing.
    for (int i = 0; i < numBypassable; ++i)
    {
        const juce::String key = bypassKeys[(size_t) i];
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { key + ".bypass", i >= numBypassable - numEffectKinds ? versionHintM5 : versionHintM4 },
                                                          key + " bypass", false,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));
    }

    for (int i = 0; i < numSoloable; ++i)
    {
        const juce::String key = bypassKeys[(size_t) i];
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { key + ".solo", versionHintM4 }, key + " solo", false,
                                                          AudioParameterBoolAttributes().withAutomatable (false)));
    }

    // Voicing: always present, not a module.
    addChoice (layout, voicingMode, "Voice mode", voiceModeNames(), 0, versionHintM2);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { voicingVoices, versionHintM2 }, "Voices", 1, maxVoices, defaultVoices));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { voicingGlide, versionHintM2 }, "Glide", juce::NormalisableRange<float> (0.0f, glideMaxSeconds, 0.001f, 0.4f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return v < 0.0005f ? juce::String ("Off") : timeText (v); })));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { voicingBendRange, versionHintM2 }, "Pitch bend range", bendRangeMin, bendRangeMax, bendRangeDefault,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (v); })));

    // Output.
    addLevel (layout, outVolume, "Output volume", volumeMinDb, volumeMaxDb, volumeDefaultDb, versionHint);

    return layout;
}

} // namespace scratchpad::params
