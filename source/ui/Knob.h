#pragma once

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

/** A rotary control bound to one parameter, with the value in the parameter's
    own text below the dial and the caption under that.

    Vertical drag, Shift for fine adjustment (both built into juce::Slider),
    double-click to reset, scroll wheel, and click the value to type one.
    One drag is one undo step, named "<name> <old> -> <new>", where the name is
    the accessible name if given (it tells Osc 2's Level from Osc 1's), else the caption.

    A knob that modulation can reach calls `setModulation`: it then shows an arc for each
    connection that moves it, and is somewhere a handle can be dropped. */
class Knob final : public juce::Component,
                   public ModTarget,
                   private ModulationModel::Listener
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
          const juce::String& captionText, juce::UndoManager& undoToUse,
          const juce::String& accessibleName = {})
        : caption (captionText), undo (undoToUse)
    {
        param = state.getParameter (paramID);
        jassert (param != nullptr);

        setPaintingIsUnclipped (true);
        getProperties().set ("explainId", paramID);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 2, pillHeight);
        slider.setTextBoxIsEditable (true);
        slider.setMouseDragSensitivity (220);
        slider.setScrollWheelEnabled (true);
        slider.setTitle (accessibleName.isEmpty() ? captionText : accessibleName);
        slider.setPaintingIsUnclipped (true);

        setPaintingIsUnclipped (true); // the caption may overhang the knob's bounds
        addAndMakeVisible (slider);

        // The attachment installs its own text conversion, so ours goes after it.
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, &undo);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        slider.valueFromTextFunction = [this] (const juce::String& text) { return parseTyped (text); };
        slider.updateText();

        slider.onDragStart = [this] { valueAtDragStart = slider.getValue(); };
        slider.onDragEnd = [this]
        {
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

    void setFamilyColour (juce::Colour c)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    }

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
    juce::Point<float> getRingCentre() const override { return { knobWidth * 0.5f, (float) (knobHeight - pillHeight - captionHeight) * 0.5f }; }
    float getDiscRadius() const override { return 14.75f; }

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
                             juce::MathConstants<float>::pi * 2.75f, arcs);
        }

        // A caption may run a little wider than the knob; neighbours are far enough apart for that.
        drawCaption (g, caption, getLocalBounds().removeFromBottom (captionHeight).expanded (captionOverhang, 0).toFloat(),
                     isEnabled() ? paletteOf (*this).mute : paletteOf (*this).mute.withMultipliedAlpha (0.5f),
                     juce::Justification::centred, 0.04f);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedBottom (captionHeight));
    }

    void enablementChanged() override { repaint(); }

    static constexpr int knobWidth = 52;
    static constexpr int pillHeight = 17;
    static constexpr int captionHeight = 14;
    static constexpr int captionOverhang = 6;
    static constexpr int knobHeight = 43 + pillHeight + captionHeight;

private:
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

    /** Understands what the parameter prints: "-inf", "Off", "2.0 kHz", "300 ms", "+7 c", "50%".
        A bare number is read in the unit the current text is shown in. */
    double parseTyped (const juce::String& text) const
    {
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

    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    juce::UndoManager& undo;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    double valueAtDragStart = 0.0;

    ModulationModel* model = nullptr;
    std::vector<engine::ModDest> destinations;
    juce::RangedAudioParameter* filterEnvAmount = nullptr;
};

} // namespace scratchpad::ui
