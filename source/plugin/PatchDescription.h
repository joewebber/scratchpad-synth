#pragma once

#include "engine/EngineParams.h"

#include <juce_core/juce_core.h>

namespace scratchpad::plugin
{

/** A plain-English sentence about the patch, built from deterministic rules: what the sources are,
    what shapes them, what moves. It is a pure function of the audible patch, so the same patch always
    reads the same, and it doubles as a check that the user understands what they built.

    `p` is what the engine hears: a bypassed module is simply absent from it. */
juce::String describe (const engine::EngineParams& p);

} // namespace scratchpad::plugin
