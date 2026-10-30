#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

/** WCAG 2.x relative luminance of an opaque colour, 0 (black) to 1 (white). */
inline float relativeLuminance (juce::Colour c) noexcept
{
    const auto channel = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * channel (c.getFloatRed()) + 0.7152f * channel (c.getFloatGreen()) + 0.0722f * channel (c.getFloatBlue());
}

/** WCAG contrast ratio between two colours, 1 (the same) to 21 (black on white). Alpha is ignored: composite first. */
inline float contrastRatio (juce::Colour a, juce::Colour b) noexcept
{
    const float la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05f) / (std::min (la, lb) + 0.05f);
}

/** WCAG 2.2 AA: 4.5:1 for text, 3:1 for large text (18 px, or 14 px bold) and for the parts of controls that carry meaning. */
inline constexpr float minTextContrast = 4.5f, minLargeTextContrast = 3.0f, minGraphicContrast = 3.0f;

} // namespace scratchpad::ui
