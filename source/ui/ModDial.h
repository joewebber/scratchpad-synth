#pragma once

#include "ui/Accessibility.h"
#include "ui/ModArcs.h"
#include "ui/ModulationModel.h"
#include "ui/Knob.h"
#include "ui/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace scratchpad::ui
{

/** A dial that has no control of its own: its value is fixed (the amp stage's level is
    always full, pan always centred), so all it does is show what modulation does to it,
    and take a handle dropped on it. Drawn like a `Knob`, at the same size. A screen reader gets its name, its
    fixed value, and which connections move it. It takes no keyboard focus: the connection is made from the
    source's handle, which lists this as a destination. */
class ModDial final : public juce::Component,
                      public ModTarget,
                      private ModulationModel::Listener
{
public:
    /** `base` is the dial's value in the units `modulatedValue` uses for `dest`, `toNormalised`
        maps such a value to 0..1 of the dial's travel, and `valueText` is printed in the pill. */
    ModDial (ModulationModel& m, engine::ModDest d, const juce::String& captionText, const juce::String& accessibleName,
             float baseValue, std::function<float (float)> toNormalisedFn, const juce::String& valueTextToShow)
        : model (m), dest (d), caption (captionText), base (baseValue), toNormalised (std::move (toNormalisedFn)),
          valueText (valueTextToShow)
    {
        setPaintingIsUnclipped (true);
        setTitle (accessibleName);
        model.registerTarget (this);
        model.addListener (this);
    }

    ~ModDial() override
    {
        model.removeListener (this);
        model.unregisterTarget (this);
    }

    std::span<const engine::ModDest> getDestinations() const override { return { &dest, 1 }; }
    juce::Component& getComponent() override { return *this; }
    juce::Point<float> getRingCentre() const override { return { Knob::knobWidth * 0.5f, discAreaHeight() * 0.5f }; }
    float getDiscRadius() const override { return 14.75f; }

    std::vector<ModArc> getArcs() const override
    {
        return model.arcsFor (getDestinations(), base, toNormalised);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        const auto centre = getRingCentre();

        constexpr float stroke = 2.5f, ringGap = 2.5f, margin = 3.0f;
        const float outer = std::min ((float) Knob::knobWidth, discAreaHeight());
        const float discSize = outer - 2.0f * (margin + ringGap + stroke * 0.5f);
        const float ringRadius = discSize * 0.5f + ringGap;
        const float startAngle = juce::MathConstants<float>::pi * 1.25f, endAngle = juce::MathConstants<float>::pi * 2.75f;
        const float angle = startAngle + toNormalised (base) * (endAngle - startAngle);

        drawRaised (g, juce::Rectangle<float> (discSize, discSize).withCentre (centre), discSize * 0.5f, p, Elevation::small, p.surface);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (p.track);
        g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (p.ink);
        g.drawLine ({ centre.x + std::sin (angle) * discSize * 0.12f, centre.y - std::cos (angle) * discSize * 0.12f,
                      centre.x + std::sin (angle) * discSize * 0.36f, centre.y - std::cos (angle) * discSize * 0.36f }, 2.2f);

        const auto arcs = getArcs();
        if (! arcs.empty())
            drawModArcs (g, p, centre, startAngle, endAngle, arcs);

        const auto pill = juce::Rectangle<float> (1.0f, discAreaHeight(), (float) Knob::knobWidth - 2.0f, (float) Knob::pillHeight);
        drawInset (g, pill, 8.0f, p, p.well);
        g.setColour (p.mute);
        g.setFont (uiFont (11.0f));
        g.drawText (valueText, pill.reduced (1.0f, 0.0f), juce::Justification::centred, false);

        drawCaption (g, caption, getLocalBounds().removeFromBottom (Knob::captionHeight).expanded (Knob::captionOverhang, 0).toFloat(),
                     p.mute, juce::Justification::centred, 0.04f);
    }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<a11y::DescribedHandler> (
            *this, juce::AccessibilityRole::staticText,
            [this]
            {
                return ("Fixed: it shows the effect of modulation and cannot be turned. " + describeArcs (getArcs())).trim();
            },
            juce::AccessibilityActions {},
            juce::AccessibilityHandler::Interfaces { std::make_unique<a11y::ReadOnlyTextValue> (
                [this] { return valueText == "C" ? juce::String ("centre") : a11y::spokenValue (valueText); }) });
    }

    static constexpr float discAreaHeight() { return (float) (Knob::knobHeight - Knob::pillHeight - Knob::captionHeight); }

    void modulationChanged() override { repaint(); }
    void liveUpdated() override { if (! getArcs().empty()) repaint(); }

    void connectingChanged() override
    {
        setAlpha (model.isConnecting() && ! model.isValidTarget (*this, model.getConnectingSource()) ? 0.3f : 1.0f);
    }

    ModulationModel& model;
    engine::ModDest dest;
    juce::String caption;
    float base;
    std::function<float (float)> toNormalised;
    juce::String valueText;
};

} // namespace scratchpad::ui
