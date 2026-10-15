#include "ui/ModArcs.h"

namespace scratchpad::ui
{

using engine::ModSource;

namespace
{
    bool isLfo (ModSource s) { return s == ModSource::lfo1 || s == ModSource::lfo2; }
}

juce::Colour sourceFill (const Palette& p, ModSource s)
{
    return isLfo (s) ? p.mod : s == ModSource::ampEnv ? p.amp : s == ModSource::filterEnv ? p.flt : p.perf;
}

juce::Colour sourceInk (const Palette& p, ModSource s)
{
    return isLfo (s) ? p.modInk : s == ModSource::ampEnv ? p.ampInk : s == ModSource::filterEnv ? p.fltInk : p.perfInk;
}

void drawModArcs (juce::Graphics& g, const Palette& p, juce::Point<float> centre, float startAngle, float endAngle,
                  const std::vector<ModArc>& arcs)
{
    const float sweep = endAngle - startAngle;

    for (size_t i = 0; i < arcs.size(); ++i)
    {
        const auto& arc = arcs[i];
        const float radius = modArcRadius (i);
        const auto colour = sourceInk (p, arc.source);

        // The range the connection can move the knob over. A range too short to see still gets a stub.
        const float from = startAngle + arc.low * sweep;
        const float to = juce::jmax (startAngle + arc.high * sweep, from + 0.05f);

        juce::Path range;
        range.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
        g.setColour (colour);
        g.strokePath (range, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Where the knob is right now.
        const float angle = startAngle + arc.live * sweep;
        const auto dot = juce::Point<float> (centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle));
        g.setColour (p.ink);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot), 1.0f);
    }
}

} // namespace scratchpad::ui
