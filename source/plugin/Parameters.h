#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Parameter IDs are a public contract with saved sessions and the copy file:
    never rename one after the first release. */
namespace scratchpad::params
{

inline constexpr const char* osc1Enabled = "osc1.enabled";
inline constexpr const char* osc1Wave    = "osc1.wave";
inline constexpr const char* osc1Level   = "osc1.level";
inline constexpr const char* outVolume   = "output.volume";

/** Version hint for every parameter created in the first release. */
inline constexpr int versionHint = 1;

inline constexpr float oscLevelMinDb = -60.0f;
inline constexpr float oscLevelMaxDb = 0.0f;
inline constexpr float oscLevelDefaultDb = -6.0f;

inline constexpr float volumeMinDb = -60.0f;
inline constexpr float volumeMaxDb = 6.0f;
inline constexpr float volumeDefaultDb = 0.0f;

/** Choice order matches `scratchpad::dsp::Wave`. */
juce::StringArray waveNames();

/** Formats a level in dB for display: "-6.0 dB", or "-inf" at the bottom of the range. */
juce::String levelText (float db, float minDb);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace scratchpad::params
