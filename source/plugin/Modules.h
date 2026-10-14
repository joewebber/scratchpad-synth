#pragma once

#include <array>
#include <cstddef>

namespace scratchpad::plugin
{

/** The modules a user adds to the one oscillator the wave picker creates.
    Each has an `enabled` parameter (see Parameters.h). The integer values
    index `allModules`. */
enum class Module
{
    osc2 = 0,
    osc3,
    noise,
    filter,
    ampEnv,
    filterEnv
};

inline constexpr std::array<Module, 6> allModules {
    Module::osc2, Module::osc3, Module::noise, Module::filter, Module::ampEnv, Module::filterEnv
};

} // namespace scratchpad::plugin
