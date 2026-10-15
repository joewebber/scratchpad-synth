#pragma once

#include "ui/ModulationModel.h"
#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace scratchpad::ui
{

/** The colour a source is drawn in, by family: LFOs are modulation, the envelopes keep the
    colour of the module they belong to, and the performance sources use the performance
    colour. `ink` is the readable variant for thin lines and text. */
juce::Colour sourceFill (const Palette&, engine::ModSource);
juce::Colour sourceInk (const Palette&, engine::ModSource);

/** Concentric arcs around a knob: for each connection the range it can move the knob over,
    and a dot where the knob is right now. The first is nearest the knob. Angles are the
    knob's own, measured clockwise from twelve o'clock. */
void drawModArcs (juce::Graphics&, const Palette&, juce::Point<float> centre, float startAngle, float endAngle,
                  const std::vector<ModArc>&);

/** Radius of arc number `i`. */
constexpr float modArcRadius (size_t i) noexcept
{
    return ModTarget::firstArcRadius + (float) i * ModTarget::arcSpacing;
}

} // namespace scratchpad::ui
