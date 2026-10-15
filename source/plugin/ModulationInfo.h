#pragma once

#include "engine/Modulation.h"
#include "plugin/Modules.h"

#include <juce_core/juce_core.h>

#include <optional>

/** Names for the modulation sources and destinations, and which module each
    belongs to. Shared by the processor (undo step names, removing a module
    removes its connections) and the editor. */
namespace scratchpad::plugin::modulation
{

/** "L1", "FE": the short tag on a source's handle, cables and rings. Never the only
    carrier of the source's identity, but small enough to sit on a ring. */
juce::String sourceLabel (engine::ModSource);

/** "LFO 1", "Filter Envelope". */
juce::String sourceName (engine::ModSource);

/** "Cutoff", "Osc 2 pitch": what a connection moves, as the knob is captioned. */
juce::String destinationName (engine::ModDest);

/** The module a source belongs to, if it can be removed. Velocity, wheel, aftertouch and key are always there. */
std::optional<Module> moduleOf (engine::ModSource);

/** The module a destination belongs to, if it can be removed: Osc 1 and the amp stage are always there. */
std::optional<Module> moduleOf (engine::ModDest);

/** "+50%", "-20%". `amount` is a fraction, -1 .. +1. */
juce::String amountText (float amount);

/** "L1 -> Cutoff +50%" with a real arrow, for undo step names. */
juce::String connectionText (const engine::ModConnection&);

} // namespace scratchpad::plugin::modulation
