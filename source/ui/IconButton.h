#pragma once

#include "ui/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** A raised button showing a stroked SVG-path glyph (drawn on a square
    `viewBox`), optionally followed by a text label. Same look as the
    prototype's `.ib` buttons. */
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& name, const char* svgPath, float viewBoxSize = 16.0f,
                float strokeWidth = 1.4f, const juce::String& labelText = {})
        : juce::Button (name),
          glyph (juce::Drawable::parseSVGPath (svgPath)),
          viewBox (viewBoxSize),
          stroke (strokeWidth),
          label (labelText)
    {
        setPaintingIsUnclipped (true);
        setTitle (name);
    }

    void setLabel (const juce::String& newLabel)
    {
        label = newLabel;
        repaint();
    }

    /** Width that fits the glyph and label with 12 px side padding. */
    int getIdealWidth() const
    {
        if (label.isEmpty())
            return 32;

        return 12 + (int) viewBox + 6 + (int) std::ceil (textWidth (uiFont (12.0f), label)) + 12;
    }

    void setGlyphColour (juce::Colour c)
    {
        glyphColour = c;
        useGlyphColour = true;
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& laf = getLookAndFeel();
        laf.drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

        const auto& p = paletteOf (*this);
        auto colour = useGlyphColour ? glyphColour : p.ink;
        if (! isEnabled())
            colour = colour.withMultipliedAlpha (0.4f);

        const auto bounds = getLocalBounds().toFloat();
        const float labelWidth = label.isEmpty() ? 0.0f : textWidth (uiFont (12.0f), label);
        const float contentWidth = viewBox + (label.isEmpty() ? 0.0f : 6.0f + labelWidth);
        const float x0 = bounds.getCentreX() - contentWidth * 0.5f;
        const float y0 = bounds.getCentreY() - viewBox * 0.5f;

        auto path = glyph;
        path.applyTransform (juce::AffineTransform::translation (x0, y0));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (label.isNotEmpty())
        {
            g.setFont (uiFont (12.0f));
            g.drawText (label, juce::Rectangle<float> (x0 + viewBox + 6.0f, bounds.getY(), labelWidth + 2.0f, bounds.getHeight()),
                        juce::Justification::centredLeft, false);
        }
    }

private:
    juce::Path glyph;
    float viewBox, stroke;
    juce::String label;
    juce::Colour glyphColour;
    bool useGlyphColour = false;
};

} // namespace scratchpad::ui
