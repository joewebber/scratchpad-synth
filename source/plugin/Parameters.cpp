#include "plugin/Parameters.h"

namespace scratchpad::params
{

juce::StringArray waveNames()
{
    return { "Sine", "Triangle", "Saw", "Pulse" };
}

juce::String levelText (float db, float minDb)
{
    if (db <= minDb)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e")) + " dB"; // "−∞ dB"

    return juce::String (db, 1) + " dB";
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using juce::AudioParameterBool;
    using juce::AudioParameterBoolAttributes;
    using juce::AudioParameterChoice;
    using juce::AudioParameterChoiceAttributes;
    using juce::AudioParameterFloat;
    using juce::AudioParameterFloatAttributes;
    using juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Module presence: not automatable, set when the user adds the module.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { osc1Enabled, versionHint }, "Oscillator 1 present", false,
        AudioParameterBoolAttributes().withAutomatable (false)));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { osc1Wave, versionHint }, "Oscillator 1 wave", waveNames(), 2,
        AudioParameterChoiceAttributes().withAutomatable (false)));

    auto levelRange = juce::NormalisableRange<float> (oscLevelMinDb, oscLevelMaxDb, 0.1f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { osc1Level, versionHint }, "Oscillator 1 level", levelRange, oscLevelDefaultDb,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return levelText (v, oscLevelMinDb); })));

    auto volumeRange = juce::NormalisableRange<float> (volumeMinDb, volumeMaxDb, 0.1f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { outVolume, versionHint }, "Output volume", volumeRange, volumeDefaultDb,
        AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return levelText (v, volumeMinDb); })));

    return layout;
}

} // namespace scratchpad::params
