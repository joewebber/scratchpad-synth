#pragma once

#include "ui/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** A small raised square with one letter in a module's header: "?" (explain), "B" (bypass) or
    "S" (solo). Toggling ones fill when on: bypass dark, solo in the accent yellow, as in the design. */
class HeaderToggle final : public juce::Button
{
public:
    enum class Kind { explain, bypass, solo };

    HeaderToggle (Kind k, const juce::String& accessibleName)
        : juce::Button (accessibleName), kind (k)
    {
        setTitle (accessibleName);

        if (k == Kind::bypass)
            getProperties().set ("explainId", "control.bypass");
        else if (k == Kind::solo)
            getProperties().set ("explainId", "control.solo");
        setPaintingIsUnclipped (true);
        setClickingTogglesState (false);
    }

    void setOn (bool shouldBeOn)
    {
        if (on != shouldBeOn)
        {
            on = shouldBeOn;
            repaint();
        }
    }

    bool isOn() const noexcept { return on; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

        juce::Colour fill = p.surface;
        juce::Colour textColour = kind == Kind::explain ? p.mute : p.ink2;

        if (on)
        {
            fill = kind == Kind::bypass ? p.ink : p.accent;
            textColour = kind == Kind::bypass ? p.surface : p.onAccent;
        }
        else if (down)
            fill = fill.interpolatedWith (p.well, 0.5f);
        else if (highlighted)
            fill = fill.interpolatedWith (p.ink, 0.06f);

        drawRaised (g, bounds, 7.0f, p, Elevation::small, fill);

        g.setColour (textColour);
        g.setFont (uiFont (12.0f, Weight::medium));
        g.drawText (kind == Kind::explain ? "?" : kind == Kind::bypass ? "B" : "S", bounds, juce::Justification::centred, false);

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, bounds, 7.0f, p.ink);
    }

private:
    Kind kind;
    bool on = false;
};

} // namespace scratchpad::ui
