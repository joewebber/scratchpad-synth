#pragma once

#include "ui/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace scratchpad::ui
{

/** A small on/off switch with a text label, bound to a bool parameter.
    A click is one undo step named "<accessible name> on" or "... off". */
class ToggleChip final : public juce::Button
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                const juce::String& labelText, const juce::String& accessibleName,
                juce::UndoManager& undoToUse, juce::Colour Palette::* familyColour)
        : juce::Button (labelText), label (labelText), undo (undoToUse), family (familyColour)
    {
        auto* param = state.getParameter (paramID);
        jassert (param != nullptr);

        setTitle (accessibleName);
        setClickingTogglesState (true);
        attachment = std::make_unique<juce::ButtonParameterAttachment> (*param, *this, &undo);

        // Runs after the attachment has written the value.
        onClick = [this] { undo.setCurrentTransactionName (getTitle() + (getToggleState() ? " on" : " off")); };
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        const bool on = getToggleState();

        const auto track = juce::Rectangle<float> (24.0f, 13.0f).withCentre ({ bounds.getX() + 12.0f, bounds.getCentreY() });
        drawInset (g, track, 6.5f, p, on ? (p.*family).withMultipliedAlpha (0.35f) : p.well);

        const float knobX = on ? track.getRight() - 7.0f : track.getX() + 7.0f;
        g.setColour (on ? p.*family : p.mute);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ knobX, track.getCentreY() }));

        g.setColour (highlighted ? p.ink : p.ink2);
        g.setFont (uiFont (11.0f));
        g.drawText (label, bounds.withTrimmedLeft (32.0f), juce::Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, bounds, 6.0f, p.ink);
    }

private:
    juce::String label;
    juce::UndoManager& undo;
    juce::Colour Palette::* family;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

/** The prototype's dashed placeholder where a module can be added. Two looks:
    a one-line chip ("+ Noise"), or a taller slot with a plus disc, a caption
    and a one-line hint. */
class SlotButton final : public juce::Button
{
public:
    enum class Style { chip, slot };

    SlotButton (const juce::String& accessibleName, const juce::String& captionText,
                Style styleToUse, const juce::String& hintText = {})
        : juce::Button (accessibleName), caption (captionText), hint (hintText), style (styleToUse)
    {
        setTitle (accessibleName);
        setPaintingIsUnclipped (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        const float radius = style == Style::chip ? 10.0f : 12.0f;

        if (highlighted || down)
        {
            g.setColour (p.ink.withAlpha (down ? 0.08f : 0.04f));
            g.fillRoundedRectangle (bounds, radius);
        }

        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), radius);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (p.dash);
        g.fillPath (dashed);

        if (style == Style::chip)
        {
            g.setColour (p.mute);
            g.setFont (uiFont (12.0f));
            g.drawText ("+ " + caption, bounds, juce::Justification::centred, true);
        }
        else
        {
            const float blockHeight = 34.0f + 6.0f + 16.0f + (hint.isNotEmpty() ? 36.0f : 0.0f);
            const float top = bounds.getCentreY() - blockHeight * 0.5f;
            const auto disc = juce::Rectangle<float> (34.0f, 34.0f).withPosition (bounds.getCentreX() - 17.0f, top);
            drawRaised (g, disc, 17.0f, p, Elevation::small, p.surface);
            g.setColour (p.mute);
            g.setFont (uiFont (18.0f));
            g.drawText ("+", disc, juce::Justification::centred, false);

            const auto textArea = bounds.reduced (8.0f, 0.0f);
            drawCaption (g, caption, { textArea.getX(), top + 40.0f, textArea.getWidth(), 16.0f }, p.ink2,
                         juce::Justification::centred);
            g.setColour (p.mute);
            g.setFont (uiFont (12.0f));
            g.drawFittedText (hint, juce::Rectangle<float> (textArea.getX(), top + 58.0f, textArea.getWidth(), 34.0f).toNearestInt(),
                              juce::Justification::centredTop, 2);
        }

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, bounds, radius, p.ink);
    }

private:
    juce::String caption, hint;
    Style style;
};

} // namespace scratchpad::ui
