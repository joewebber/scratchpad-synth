#pragma once

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui
{

enum class Weight { light, regular, medium };

/** Jost (SIL OFL, embedded) at `size` CSS pixels, so numbers copied from the
    design prototype's `font-size` carry over unchanged. */
juce::Font uiFont (float size, Weight = Weight::regular);

/** Small-caps style caption used throughout: 11 px, tracked out, upper case. */
void drawCaption (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                  juce::Colour, juce::Justification = juce::Justification::centredLeft,
                  float tracking = 0.14f);

/** Advance width of `text` in `font`, including its tracking. */
float textWidth (const juce::Font&, const juce::String& text);

/** Greedy word wrap of `text` to `width` pixels. */
juce::StringArray wrapLines (const juce::Font&, const juce::String& text, float width);

/** Draws pre-wrapped lines, each in its own `lineHeight` box like a CSS line
    box, top-left at `origin` (or centred on `origin.x` if `centred`). */
void drawLines (juce::Graphics&, const juce::StringArray& lines, juce::Point<float> origin,
                float width, float lineHeight, bool centred = false);

} // namespace scratchpad::ui
