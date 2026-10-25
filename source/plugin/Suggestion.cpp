#include "plugin/Suggestion.h"

namespace scratchpad::plugin
{

namespace
{
    constexpr std::array<Module, 12> path {
        Module::ampEnv, Module::osc2, Module::filter, Module::filterEnv,
        Module::lfo1, Module::noise, Module::osc3, Module::lfo2,
        Module::delay, Module::reverb, Module::chorus, Module::distortion
    };
}

const char* reasonFor (Module m)
{
    switch (m)
    {
        case Module::ampEnv:    return "Add an Amp Envelope to shape how the note starts and stops.";
        case Module::osc2:      return "Add a second oscillator and detune it: two waves a few cents apart thicken the sound.";
        case Module::filter:    return "Add a Filter to take harmonics away: the sound gets darker as the cutoff closes.";
        case Module::filterEnv: return "Add a Filter Envelope so the brightness moves over each note.";
        case Module::lfo1:      return "Add LFO 1 for movement: send it to pitch for vibrato, then to the cutoff.";
        case Module::noise:     return "Add Noise for breath and grit that the filter and envelope can shape.";
        case Module::osc3:      return "Add a third oscillator to fatten the sound further, or to play a second pitch.";
        case Module::lfo2:      return "Add LFO 2 so a second thing can move at its own speed.";
        case Module::delay:      return "Add a Delay to make the sound echo: each repeat is a quieter copy of the last.";
        case Module::reverb:     return "Add a Reverb to place the sound in a room, so notes ring on after you let go.";
        case Module::chorus:     return "Add a Chorus to widen and shimmer the sound with slowly moving copies of itself.";
        case Module::distortion: return "Add Distortion to squash the peaks and add harmonics: from warm crunch to fuzz.";
    }
    return "";
}

std::optional<Suggestion> suggestNext (const Presence& present)
{
    for (const auto m : path)
        if (! present[(size_t) m])
            return Suggestion { m, reasonFor (m) };

    return std::nullopt;
}

} // namespace scratchpad::plugin
