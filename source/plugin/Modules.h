#pragma once

#include <array>
#include <cstddef>

namespace scratchpad::plugin
{

/** The modules a user adds to oscillator 1, which every patch has.
    Each has an `enabled` parameter (see Parameters.h). The integer values
    index `allModules`. */
enum class Module
{
    osc2 = 0,
    osc3,
    noise,
    filter,
    ampEnv,
    filterEnv,
    lfo1,
    lfo2,
    distortion,
    chorus,
    delay,
    reverb
};

inline constexpr std::array<Module, 12> allModules {
    Module::osc2, Module::osc3, Module::noise, Module::filter, Module::ampEnv, Module::filterEnv, Module::lfo1, Module::lfo2,
    Module::distortion, Module::chorus, Module::delay, Module::reverb
};

} // namespace scratchpad::plugin
