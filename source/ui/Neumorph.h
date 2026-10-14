#pragma once

#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui
{

/** Which of the prototype's outer shadows a surface uses. */
enum class Elevation { raised, lifted, small };

/** Fills a rounded rectangle with `fill` and its two-layer outer shadow
    (`--sh-r`, `--sh-l` or `--sh-s`). Shadow images are cached per size. */
void drawRaised (juce::Graphics&, juce::Rectangle<float> bounds, float radius,
                 const Palette&, Elevation, juce::Colour fill);

/** Fills a rounded rectangle with `fill` and the inner shadow (`--sh-i`):
    a well the surface appears pressed into. */
void drawInset (juce::Graphics&, juce::Rectangle<float> bounds, float radius,
                const Palette&, juce::Colour fill);

} // namespace scratchpad::ui
