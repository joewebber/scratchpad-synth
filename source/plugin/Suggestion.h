#pragma once

#include "plugin/Modules.h"

#include <array>
#include <optional>

namespace scratchpad::plugin
{

/** The module the Add palette highlights, and one sentence on why. Nothing is ever locked: this is advice. */
struct Suggestion
{
    Module module;
    const char* reason;
};

/** Which modules are in the patch, indexed by `Module`. */
using Presence = std::array<bool, allModules.size()>;

/** The next step of the suggested path: amp envelope, a second oscillator, filter, filter envelope, LFO 1,
    noise, a third oscillator, LFO 2, then the effects (delay, reverb, chorus, distortion). Modules the user has already added are skipped, in any order, so
    someone who jumps ahead is advised about what is still missing. Empty once everything is in the patch. */
std::optional<Suggestion> suggestNext (const Presence&);

/** The reason for suggesting `m`, whether or not it is the current suggestion. */
const char* reasonFor (Module m);

} // namespace scratchpad::plugin
