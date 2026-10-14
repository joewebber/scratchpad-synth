#include "ui/Typography.h"

#include <FontData.h>

namespace scratchpad::ui
{

namespace
{
    juce::Typeface::Ptr typefaceFor (Weight w)
    {
        static const juce::Typeface::Ptr light = juce::Typeface::createSystemTypefaceFor (
            fontdata::Jost300Light_ttf, (size_t) fontdata::Jost300Light_ttfSize);
        static const juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (
            fontdata::Jost400Book_ttf, (size_t) fontdata::Jost400Book_ttfSize);
        static const juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (
            fontdata::Jost500Medium_ttf, (size_t) fontdata::Jost500Medium_ttfSize);

        switch (w)
        {
            case Weight::light:  return light;
            case Weight::medium: return medium;
            case Weight::regular: break;
        }
        return regular;
    }
}

juce::Font uiFont (float size, Weight w)
{
    return juce::Font (juce::FontOptions (typefaceFor (w)).withPointHeight (size));
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  juce::Colour colour, juce::Justification justification, float tracking)
{
    auto font = uiFont (11.0f);
    font.setExtraKerningFactor (tracking);
    g.setFont (font);
    g.setColour (colour);
    g.drawText (text.toUpperCase(), area, justification, false);
}

float textWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox (0, -1, false).getWidth();
}

juce::StringArray wrapLines (const juce::Font& font, const juce::String& text, float width)
{
    juce::StringArray lines, words;
    words.addTokens (text, " ", "");
    words.removeEmptyStrings();

    juce::String line;
    for (const auto& word : words)
    {
        const auto candidate = line.isEmpty() ? word : line + " " + word;
        if (line.isNotEmpty() && textWidth (font, candidate) > width)
        {
            lines.add (line);
            line = word;
        }
        else
        {
            line = candidate;
        }
    }

    if (line.isNotEmpty())
        lines.add (line);

    return lines;
}

void drawLines (juce::Graphics& g, const juce::StringArray& lines, juce::Point<float> origin,
                float width, float lineHeight, bool centred)
{
    float y = origin.y;
    for (const auto& line : lines)
    {
        const auto box = centred ? juce::Rectangle<float> (origin.x - width * 0.5f, y, width, lineHeight)
                                 : juce::Rectangle<float> (origin.x, y, width, lineHeight);
        g.drawText (line, box, centred ? juce::Justification::centred : juce::Justification::centredLeft, false);
        y += lineHeight;
    }
}

} // namespace scratchpad::ui
