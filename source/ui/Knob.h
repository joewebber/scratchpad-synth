#pragma once

#include "content/Content.h"
#include "plugin/ModulationInfo.h"
#include "ui/Accessibility.h"
#include "ui/KeyboardSlider.h"
#include "ui/Layout.h"
#include "ui/LookAndFeel.h"
#include "ui/ModArcs.h"
#include "ui/ModulationModel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** The size of a knob's component: its disc's cell (the diameter and a margin each side, square) with the one line
    of text under it. Used by `Knob`, `ModDial` and the cards that lay them out. */
struct KnobGeometry
{
    static constexpr int cellFor (const layout::KnobSize& size) noexcept { return size.diameter + 2 * knobMetrics::margin; }
    static constexpr int heightFor (const layout::KnobSize& size) noexcept { return cellFor (size) + knobMetrics::labelGap + knobMetrics::labelHeight; }
    static constexpr float firstArcRadiusFor (const layout::KnobSize& size) noexcept { return (float) size.diameter * 0.5f + 6.0f; }
};

/** A rotary control bound to one parameter. At rest the line under the dial is its caption; while the pointer is
    over it, it is being dragged or it has the keyboard focus, the parameter's own text replaces the caption. A click
    on the value (when it shows) turns it into a typed entry. The component is `layout::KnobSize::cell` wide and
    `height()` tall.

    Vertical drag, Shift for fine adjustment (both built into juce::Slider),
    double-click to reset, scroll wheel, and click the value to type one.
    From the keyboard it is a `KeyboardSlider`: arrow keys, Page Up and Down, Home and End, Delete to reset,
    and Enter to type a value.
    One drag is one undo step, named "<name> <old> -> <new>", where the name is
    the accessible name if given (it tells Osc 2's Level from Osc 1's), else the caption.

    A screen reader hears the accessible name, the value in words, and a description: what the parameter does
    (the Explain panel's sentence) and, if connections move it, which ones and how far.

    A knob that modulation can reach calls `setModulation`: it then shows an arc for each
    connection that moves it, and is somewhere a handle can be dropped. */
class Knob final : public juce::Component,
                   public ModTarget,
                   private ModulationModel::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
          const juce::String& captionText, juce::UndoManager& undoToUse,
          const juce::String& accessibleName = {}, layout::KnobSize knobSize = layout::knobs::standard)
        : size (knobSize), caption (captionText), paramId (paramID), undo (undoToUse)
    {
        param = state.getParameter (paramID);
        jassert (param != nullptr);

        setPaintingIsUnclipped (true);
        getProperties().set ("explainId", paramID);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, size.cell, knobMetrics::labelHeight);
        slider.getProperties().set (knobMetrics::discSize, size.diameter);
        slider.setTextBoxIsEditable (true);
        slider.setMouseDragSensitivity (220);
        slider.setScrollWheelEnabled (true);
        slider.setTitle (accessibleName.isEmpty() ? captionText : accessibleName);
        slider.setPaintingIsUnclipped (true);
        slider.describe = [this] { return describeKnob(); };

        setPaintingIsUnclipped (true); // the caption may overhang the knob's bounds
        addAndMakeVisible (slider);

        // The attachment installs its own text conversion, so ours goes after it.
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, &undo);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        slider.valueFromTextFunction = [this] (const juce::String& text) { return parseTyped (text); };
        slider.updateText();
        slider.tidyValueLabel();

        slider.addMouseListener (this, true);
        slider.onFaceChange = [this] { refreshFace(); };
        slider.beforeTyping = [this] { forceValue = true; refreshFace(); };
        refreshFace();

        slider.onDragStart = [this] { valueAtDragStart = slider.getValue(); dragging = true; refreshFace(); };
        slider.onDragEnd = [this]
        {
            dragging = false;
            refreshFace();

            if (std::abs (slider.getValue() - valueAtDragStart) > 1.0e-6)
                undo.setCurrentTransactionName (slider.getTitle() + " " + slider.getTextFromValue (valueAtDragStart)
                                                + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))
                                                + slider.getTextFromValue (slider.getValue()));
        };
    }

    ~Knob() override
    {
        if (model != nullptr)
        {
            model->removeListener (this);
            model->unregisterTarget (this);
        }
    }

    void setCaption (const juce::String& newCaption)
    {
        caption = newCaption;
        repaint();
    }

    void setFamilyColour (juce::Colour c)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    }

    /** What a screen reader is told when the knob is dimmed, in place of nothing: why it is not in use. */
    void setDisabledNote (const juce::String& why) { disabledNote = why; }

    /** The value arc runs from the middle of the travel (pitch, pan, an envelope amount). */
    void setBipolar (bool shouldBeBipolar)
    {
        slider.getProperties().set (knobMetrics::bipolar, shouldBeBipolar);
        slider.repaint();
    }

    juce::Slider& getSlider() noexcept { return slider; }

    /** Shows the caption and the value together (a pop-over's knobs, which have room): caption first, value under it. */
    void setShowsCaptionAndValue()
    {
        both = true;
        slider.getProperties().set (knobMetrics::valueY, KnobGeometry::cellFor (size) + knobMetrics::labelGap + knobMetrics::labelHeight);
        refreshFace();
    }

    /** The value is always showing and the caption never does (a table's cells: the column headings are the labels). */
    void setShowsValueOnly()
    {
        valueOnly = true;
        refreshFace();
    }

    const layout::KnobSize& getSize() const noexcept { return size; }
    int height() const noexcept { return KnobGeometry::heightFor (size) + (both ? knobMetrics::labelHeight : 0); }
    void setBoundsAt (int x, int y) { setBounds (x, y, size.cell, height()); }

    /** Makes this knob a destination for `dests` (the first is the default for a drop). With
        `hardWiredFilterEnvAmount`, the filter envelope's own amount shows as one more arc. */
    void setModulation (ModulationModel& m, std::vector<engine::ModDest> dests,
                        juce::RangedAudioParameter* hardWiredFilterEnvAmount = nullptr)
    {
        jassert (model == nullptr);
        model = &m;
        destinations = std::move (dests);
        filterEnvAmount = hardWiredFilterEnvAmount;
        model->registerTarget (this);
        model->addListener (this);
    }

    // ModTarget
    std::span<const engine::ModDest> getDestinations() const override { return destinations; }
    juce::Component& getComponent() override { return *this; }
    juce::Point<float> getRingCentre() const override { return { (float) getWidth() * 0.5f, (float) KnobGeometry::cellFor (size) * 0.5f }; }
    float getDiscRadius() const override { return (float) size.diameter * 0.5f; }
    float getFirstArcRadius() const override { return KnobGeometry::firstArcRadiusFor (size); }

    std::vector<ModArc> getArcs() const override
    {
        if (model == nullptr)
            return {};

        std::optional<float> wired;
        if (filterEnvAmount != nullptr)
            wired = filterEnvAmount->convertFrom0to1 (filterEnvAmount->getValue()) * 0.01f;

        return model->arcsFor (destinations, param->convertFrom0to1 (param->getValue()),
                               [this] (float v) { return param->convertTo0to1 (v); }, wired);
    }

    void paint (juce::Graphics& g) override
    {
        if (model != nullptr)
        {
            const auto arcs = getArcs();
            if (! arcs.empty())
                drawModArcs (g, paletteOf (*this), getRingCentre(), juce::MathConstants<float>::pi * 1.25f,
                             juce::MathConstants<float>::pi * 2.75f, arcs, getFirstArcRadius());
        }

        // The caption is the resting face of the line under the dial; the value replaces it (see `refreshFace`).
        if (! valueOnly && (! valueShowing || both))
        {
            const auto line = juce::Rectangle<float> ((float) getWidth(), (float) knobMetrics::labelHeight)
                                  .withY ((float) (KnobGeometry::cellFor (size) + knobMetrics::labelGap));
            drawCaption (g, caption, line.expanded (captionOverhang, 0.0f),
                         isEnabled() ? paletteOf (*this).mute : paletteOf (*this).mute.withMultipliedAlpha (0.5f),
                         juce::Justification::centred, 0.04f);
        }
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds());
    }

    /** The slider lays its disc and value box out from the look and feel, which a knob built outside the editor (in a
        pop-over, before it is shown) only gets once it has a parent. */
    void parentHierarchyChanged() override { slider.resized(); }

    void mouseEnter (const juce::MouseEvent&) override { refreshFace(); }
    void mouseExit (const juce::MouseEvent&) override  { refreshFace(); }

    void enablementChanged() override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        // The slider inside carries the name, value and description; the box round it adds nothing.
        return a11y::makeIgnoredHandler (*this);
    }

    static constexpr int captionOverhang = 6;

private:
    /** Value while hovered, dragged, focused or being typed; the caption otherwise. */
    void refreshFace()
    {
        const bool editing = slider.findValueLabel() != nullptr && slider.findValueLabel()->isBeingEdited();
        const bool show = isEnabled() && (both || valueOnly || slider.isMouseOver (true) || dragging || slider.hasKeyboardFocus (false) || editing || forceValue);
        forceValue = editing;

        if (auto* label = slider.findValueLabel())
            label->setVisible (show);

        if (show != valueShowing)
        {
            valueShowing = show;
            repaint();
        }
    }

    void modulationChanged() override { repaint(); }
    void liveUpdated() override { if (hasArcs()) repaint(); }

    void connectingChanged() override
    {
        // While a handle is being dragged, the knobs it can reach stay lit and the rest fade back.
        const bool dim = model->isConnecting() && ! model->isValidTarget (*this, model->getConnectingSource());
        setAlpha (dim ? 0.3f : 1.0f);
    }

    bool hasArcs() const
    {
        if (filterEnvAmount != nullptr && model->getProcessor().isModulePresent (plugin::Module::filterEnv))
            return true;

        for (const auto& c : model->getConnections())
            if (c.active() && std::find (destinations.begin(), destinations.end(), c.dest) != destinations.end())
                return true;

        return false;
    }

    /** What the knob does, then which connections move it and how far. */
    juce::String describeKnob() const
    {
        juce::String text;

        if (const auto* entry = content::Content::get().param (paramId))
            text = entry->what;

        if (! slider.isEnabled() && disabledNote.isNotEmpty())
            text = disabledNote + " " + text;

        if (model == nullptr)
            return text;

        return (text + " " + describeArcs (getArcs())).trim();
    }

    /** Understands what the parameter prints: "-inf", "Off", "2.0 kHz", "300 ms", "+7 c", "50%".
        A bare number is read in the unit the current text is shown in. */
    double parseTyped (const juce::String& text) const
    {
        // A stepped choice ("1/8", "2 bars", "Osc 3"): match the typed words, else leave it where it is.
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            const int found = choice->choices.indexOf (text.trim(), true);
            return (double) (found >= 0 ? found : choice->getIndex());
        }

        const auto range = param->getNormalisableRange();
        const auto t = text.trim().toLowerCase().replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");

        if (t.contains ("inf") || t.contains (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"))) || t.startsWith ("off"))
            return (double) range.start;

        const auto number = t.removeCharacters (" ").getDoubleValue();
        const auto current = param->getText (param->getValue(), 0).toLowerCase();

        const bool typedKilo = t.contains ("k");
        const bool typedMilli = t.contains ("ms");
        const bool typedUnit = typedKilo || typedMilli || t.containsAnyOf ("hzs");

        const bool kilo = typedUnit ? typedKilo : current.contains ("khz");
        const bool milli = typedUnit ? typedMilli : current.contains ("ms");

        return kilo ? number * 1000.0 : milli ? number / 1000.0 : number;
    }

    layout::KnobSize size;
    bool dragging = false, forceValue = false, valueShowing = false, both = false, valueOnly = false;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption, paramId, disabledNote;
    juce::UndoManager& undo;
    KeyboardSlider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    double valueAtDragStart = 0.0;

    ModulationModel* model = nullptr;
    std::vector<engine::ModDest> destinations;
    juce::RangedAudioParameter* filterEnvAmount = nullptr;
};

} // namespace scratchpad::ui
