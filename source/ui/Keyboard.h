#pragma once

#include "ui/Accessibility.h"
#include "ui/LookAndFeel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace scratchpad::ui
{

/** The on-screen keyboard drawn in the prototype's style: palette-coloured
    keys with rounded bottoms and a small shadow, labelled with the computer
    key that plays them. It is played with the mouse or those computer keys, so it takes no keyboard focus of its
    own; a screen reader is told how to play it by `describe`. */
class Keyboard final : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    std::function<juce::String()> describe;

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group, describe);
    }

    void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour, juce::Colour) override
    {
        const auto& p = paletteOf (*this);
        // Leave room under the key for its shadow: the keyboard clips to its own bounds.
        auto key = area.reduced (0.5f, 0.0f).withTrimmedBottom (shadowRoom);
        constexpr float radius = 7.0f;

        juce::Colour fill = isDown ? p.accent : p.keyWhite;
        if (isOver && ! isDown)
            fill = fill.interpolatedWith (p.ink, 0.06f);

        // Extend above the visible edge so only the bottom corners read as rounded.
        drawRaised (g, key.withTop (key.getY() - radius * 2.0f), radius, p, Elevation::small, fill);
        drawLabel (g, note, key.withTrimmedBottom (5.0f), isDown ? p.onAccent : p.mute);
    }

    void drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour) override
    {
        const auto& p = paletteOf (*this);
        constexpr float radius = 5.0f;

        juce::Colour fill = isDown ? p.accent : p.keyBlack;
        if (isOver && ! isDown)
            fill = fill.interpolatedWith (p.ink, 0.12f);

        drawRaised (g, area.withTop (area.getY() - radius * 2.0f), radius, p, Elevation::small, fill);
        drawLabel (g, note, area.withTrimmedBottom (4.0f), isDown ? p.onAccent : p.keyBlackInk);
    }

private:
    static constexpr float shadowRoom = 8.0f;

    /** The letter on the computer keyboard that plays `semitoneAboveBase`, or 0. */
    static char computerKeyFor (int semitoneAboveBase)
    {
        switch (semitoneAboveBase)
        {
            case 0:  return 'A';  case 1:  return 'W';  case 2:  return 'S';  case 3:  return 'E';
            case 4:  return 'D';  case 5:  return 'F';  case 6:  return 'T';  case 7:  return 'G';
            case 8:  return 'Y';  case 9:  return 'H';  case 10: return 'U';  case 11: return 'J';
            case 12: return 'K';
            default: return 0;
        }
    }

    void drawLabel (juce::Graphics& g, int note, juce::Rectangle<float> area, juce::Colour colour) const
    {
        if (const char letter = computerKeyFor (note - getRangeStart()); letter != 0)
        {
            g.setColour (colour);
            g.setFont (uiFont (11.0f));
            g.drawText (juce::String::charToString ((juce::juce_wchar) letter), area,
                        juce::Justification::centredBottom, false);
        }
    }
};

} // namespace scratchpad::ui
