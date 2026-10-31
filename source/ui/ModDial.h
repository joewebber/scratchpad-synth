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
             float baseValue, std::function<float (float)> toNormalisedFn, const juce::String& valueTextToShow,
             layout::KnobSize knobSize = layout::knobs::standard)
        : size (knobSize), model (m), dest (d), caption (captionText), base (baseValue), toNormalised (std::move (toNormalisedFn)),
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
    juce::Point<float> getRingCentre() const override { return { (float) getWidth() * 0.5f, (float) KnobGeometry::cellFor (size) * 0.5f }; }
    float getDiscRadius() const override { return (float) size.diameter * 0.5f; }
    float getFirstArcRadius() const override { return KnobGeometry::firstArcRadiusFor (size); }

    int height() const noexcept { return KnobGeometry::heightFor (size); }
    void setBoundsAt (int x, int y) { setBounds (x, y, size.cell, height()); }

    void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

    std::vector<ModArc> getArcs() const override
    {
        return model.arcsFor (getDestinations(), base, toNormalised);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        const auto centre = getRingCentre();

        constexpr float stroke = 2.4f, ringGap = 2.5f;
        const float discSize = (float) size.diameter;
        const float ringRadius = discSize * 0.5f + ringGap;
        const float startAngle = juce::MathConstants<float>::pi * 1.25f, endAngle = juce::MathConstants<float>::pi * 2.75f;
        const float angle = startAngle + toNormalised (base) * (endAngle - startAngle);

        drawRaised (g, juce::Rectangle<float> (discSize, discSize).withCentre (centre), discSize * 0.5f, p, Elevation::small, p.knob);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
        g.setColour (p.track);
        g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (p.ink);
        g.drawLine ({ centre.x + std::sin (angle) * discSize * 0.1f, centre.y - std::cos (angle) * discSize * 0.1f,
                      centre.x + std::sin (angle) * discSize * 0.36f, centre.y - std::cos (angle) * discSize * 0.36f }, 2.0f);

        const auto arcs = getArcs();
        if (! arcs.empty())
            drawModArcs (g, p, centre, startAngle, endAngle, arcs, getFirstArcRadius());

        // The caption at rest, the (fixed) value while the pointer is over the dial, as a `Knob` does.
        const auto line = juce::Rectangle<float> ((float) getWidth(), (float) knobMetrics::labelHeight)
                              .withY ((float) (KnobGeometry::cellFor (size) + knobMetrics::labelGap));

        if (hovering)
        {
            g.setColour (p.ink);
            g.setFont (uiFont (11.0f));
            g.drawText (valueText, line, juce::Justification::centred, false);
        }
        else
        {
            drawCaption (g, caption, line.expanded ((float) Knob::captionOverhang, 0.0f), p.mute, juce::Justification::centred, 0.04f);
        }
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

    void modulationChanged() override { repaint(); }
    void liveUpdated() override { if (! getArcs().empty()) repaint(); }

    void connectingChanged() override
    {
        setAlpha (model.isConnecting() && ! model.isValidTarget (*this, model.getConnectingSource()) ? 0.3f : 1.0f);
    }

    layout::KnobSize size;
    bool hovering = false;
    ModulationModel& model;
    engine::ModDest dest;
    juce::String caption;
    float base;
    std::function<float (float)> toNormalised;
    juce::String valueText;
};

} // namespace scratchpad::ui
