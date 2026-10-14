#pragma once

#include "plugin/Parameters.h"
#include "ui/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace scratchpad::ui
{

/** A rotary control bound to one parameter, with the value in real units
    below the dial and the caption under that.

    Vertical drag, Shift for fine adjustment (both built into juce::Slider),
    double-click to reset, scroll wheel, and click the value to type one.
    One drag is one undo step, named "<Caption> <old> -> <new>". */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
          const juce::String& captionText, float minDb, juce::UndoManager& undoToUse)
        : caption (captionText), undo (undoToUse)
    {
        auto* param = state.getParameter (paramID);
        jassert (param != nullptr);

        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 24);
        slider.setTextBoxIsEditable (true);
        slider.setMouseDragSensitivity (220);
        slider.setScrollWheelEnabled (true);
        slider.setTitle (captionText);

        addAndMakeVisible (slider);

        // The attachment installs its own text conversion, so ours goes after it.
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, &undo);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

        slider.textFromValueFunction = [minDb] (double v) { return params::levelText ((float) v, minDb); };
        slider.valueFromTextFunction = [minDb] (const juce::String& text)
        {
            auto t = text.trim().toLowerCase();
            if (t.contains ("inf") || t.contains (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e"))))
                return (double) minDb;

            const auto cleaned = t.removeCharacters (" db").replace (juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), "-");
            return (double) juce::jmax (minDb, cleaned.getFloatValue());
        };
        slider.updateText();

        slider.onDragStart = [this] { valueAtDragStart = slider.getValue(); };
        slider.onDragEnd = [this]
        {
            if (std::abs (slider.getValue() - valueAtDragStart) > 1.0e-6)
                undo.setCurrentTransactionName (caption + " " + slider.getTextFromValue (valueAtDragStart)
                                                + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))
                                                + slider.getTextFromValue (slider.getValue()));
        };
    }

    void setFamilyColour (juce::Colour c)
    {
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    }

    void paint (juce::Graphics& g) override
    {
        drawCaption (g, caption, getLocalBounds().removeFromBottom (captionHeight).toFloat(),
                     paletteOf (*this).mute, juce::Justification::centred, 0.08f);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().withTrimmedBottom (captionHeight));
    }

    static constexpr int captionHeight = 16;

private:
    juce::String caption;
    juce::UndoManager& undo;
    juce::Slider slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    double valueAtDragStart = 0.0;
};

} // namespace scratchpad::ui
