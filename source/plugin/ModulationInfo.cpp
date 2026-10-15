#include "plugin/ModulationInfo.h"

namespace scratchpad::plugin::modulation
{

using engine::ModDest;
using engine::ModSource;

namespace
{
    const juce::String arrow = juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "));
}

juce::String sourceLabel (ModSource s)
{
    switch (s)
    {
        case ModSource::none:       return {};
        case ModSource::lfo1:       return "L1";
        case ModSource::lfo2:       return "L2";
        case ModSource::ampEnv:     return "AE";
        case ModSource::filterEnv:  return "FE";
        case ModSource::velocity:   return "VEL";
        case ModSource::modWheel:   return "MW";
        case ModSource::aftertouch: return "AT";
        case ModSource::key:        return "KEY";
    }
    return {};
}

juce::String sourceName (ModSource s)
{
    switch (s)
    {
        case ModSource::none:       return {};
        case ModSource::lfo1:       return "LFO 1";
        case ModSource::lfo2:       return "LFO 2";
        case ModSource::ampEnv:     return "Amp Envelope";
        case ModSource::filterEnv:  return "Filter Envelope";
        case ModSource::velocity:   return "Velocity";
        case ModSource::modWheel:   return "Mod wheel";
        case ModSource::aftertouch: return "Aftertouch";
        case ModSource::key:        return "Key";
    }
    return {};
}

juce::String destinationName (ModDest d)
{
    switch (d)
    {
        case ModDest::none:        return {};
        case ModDest::pitchAll:    return "Pitch";
        case ModDest::pitch1:      return "Osc 1 pitch";
        case ModDest::pitch2:      return "Osc 2 pitch";
        case ModDest::pitch3:      return "Osc 3 pitch";
        case ModDest::pulseWidth1: return "Osc 1 width";
        case ModDest::pulseWidth2: return "Osc 2 width";
        case ModDest::pulseWidth3: return "Osc 3 width";
        case ModDest::level1:      return "Osc 1 level";
        case ModDest::level2:      return "Osc 2 level";
        case ModDest::level3:      return "Osc 3 level";
        case ModDest::noiseLevel:  return "Noise level";
        case ModDest::cutoff:      return "Cutoff";
        case ModDest::resonance:   return "Resonance";
        case ModDest::ampLevel:    return "Amp level";
        case ModDest::pan:         return "Pan";
        case ModDest::lfo1Rate:    return "LFO 1 rate";
        case ModDest::lfo2Rate:    return "LFO 2 rate";
    }
    return {};
}

std::optional<Module> moduleOf (ModSource s)
{
    switch (s)
    {
        case ModSource::lfo1:      return Module::lfo1;
        case ModSource::lfo2:      return Module::lfo2;
        case ModSource::ampEnv:    return Module::ampEnv;
        case ModSource::filterEnv: return Module::filterEnv;
        case ModSource::none:
        case ModSource::velocity:
        case ModSource::modWheel:
        case ModSource::aftertouch:
        case ModSource::key:       return std::nullopt;
    }

    return std::nullopt;
}

std::optional<Module> moduleOf (ModDest d)
{
    switch (d)
    {
        case ModDest::pitch2:
        case ModDest::pulseWidth2:
        case ModDest::level2:      return Module::osc2;
        case ModDest::pitch3:
        case ModDest::pulseWidth3:
        case ModDest::level3:      return Module::osc3;
        case ModDest::noiseLevel:  return Module::noise;
        case ModDest::cutoff:
        case ModDest::resonance:   return Module::filter;
        case ModDest::lfo1Rate:    return Module::lfo1;
        case ModDest::lfo2Rate:    return Module::lfo2;
        case ModDest::none:
        case ModDest::pitchAll:
        case ModDest::pitch1:
        case ModDest::pulseWidth1:
        case ModDest::level1:
        case ModDest::ampLevel:
        case ModDest::pan:         return std::nullopt;
    }

    return std::nullopt;
}

juce::String amountText (float amount)
{
    const int percent = juce::roundToInt (amount * 100.0f);
    return (percent > 0 ? "+" : "") + juce::String (percent) + "%";
}

juce::String connectionText (const engine::ModConnection& c)
{
    return sourceLabel (c.source) + arrow + destinationName (c.dest) + " " + amountText (c.amount);
}

} // namespace scratchpad::plugin::modulation
