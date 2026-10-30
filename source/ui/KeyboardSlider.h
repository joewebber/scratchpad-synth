#pragma once

#include "ui/Accessibility.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace scratchpad::ui
{

/** A slider that works from the keyboard and tells a screen reader what it is.

    Keys, while it has the focus:
      Up / Right, Down / Left   one step: 1% of the travel (a stepped control moves one step)
      Shift + those             a fine step: 0.1% of the travel (a stepped control still moves one step)
      Page Up / Page Down       a big step: 10% of the travel
      Home / End                the lowest / the highest value
      Delete / Backspace        back to the default value (what a double-click does)
      Enter                     opens the value for typing; Enter or Escape closes it and returns here

    The steps are fractions of the knob's travel as it is drawn (so a logarithmic cutoff moves evenly to the eye);
    `steps` changes them. Each key press is one undo step, like one drag.

    For a screen reader it is a slider with a range: it can be set to a value, and says its value in words
    ("+7 cents", "2.00 kHz"). Its description is asked for when needed, so it can follow what the control is doing. */
class KeyboardSlider : public juce::Slider,
                       private juce::Label::Listener
{
public:
    static constexpr double coarseStep = 0.01, fineStep = 0.001, pageStep = 0.1;

    KeyboardSlider()
    {
        setWantsKeyboardFocus (true);
    }

    ~KeyboardSlider() override
    {
        if (auto* label = findValueLabel())
            label->removeListener (this);
    }

    /** What a screen reader hears after the name and value. */
    std::function<juce::String()> describe;

    /** How far one key press moves it, as fractions of the travel. The defaults suit a knob. */
    struct Steps { double coarse = coarseStep, fine = fineStep, page = pageStep; } steps;

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto mods = key.getModifiers();

        if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown() || ! isEnabled())
            return false;

        const int code = key.getKeyCode();

        if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
            return step (+1, mods.isShiftDown() ? steps.fine : steps.coarse);

        if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
            return step (-1, mods.isShiftDown() ? steps.fine : steps.coarse);

        if (mods.isShiftDown())
            return false;

        if (code == juce::KeyPress::pageUpKey)   return step (+1, steps.page);
        if (code == juce::KeyPress::pageDownKey) return step (-1, steps.page);
        if (code == juce::KeyPress::homeKey)     return setByKey (getMinimum());
        if (code == juce::KeyPress::endKey)      return setByKey (getMaximum());

        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && isDoubleClickReturnEnabled())
            return setByKey (getDoubleClickReturnValue());

        if (code == juce::KeyPress::returnKey && isTextBoxEditable() && getTextBoxPosition() != NoTextBox)
        {
            showTextBox();
            return true;
        }

        return false;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    // The slider makes its value box again whenever its look changes or it is enabled or disabled, and each time it
    // asks for the box to take the keyboard focus. Tab should visit the knob, not its number.
    void lookAndFeelChanged() override
    {
        juce::Slider::lookAndFeelChanged();
        tidyValueLabel();
    }

    void enablementChanged() override
    {
        juce::Slider::enablementChanged();
        tidyValueLabel();
    }

    /** Call after the text box is set up (it is rebuilt when the look changes, and this is called again then). */
    void tidyValueLabel()
    {
        if (auto* label = findValueLabel())
        {
            label->setWantsKeyboardFocus (false);
            label->setFocusContainerType (FocusContainerType::none);
            label->addListener (this);
        }
    }

protected:
    /** What the value is called when read aloud. */
    virtual juce::String spokenText (double value) { return a11y::spokenValue (getTextFromValue (value)); }

    /** Whether a key press is a drag as far as undo and the parameter's gesture go. A control that springs back on
        its own when a drag ends (a pitch wheel) says no, and is moved without a gesture. */
    virtual bool keyChangeIsGesture() const { return true; }

    bool setByKey (double newValue)
    {
        newValue = juce::jlimit (getMinimum(), getMaximum(), newValue);

        if (juce::approximatelyEqual (newValue, getValue()))
            return true;

        if (keyChangeIsGesture())
        {
            ScopedDragNotification drag (*this);
            setValue (newValue, juce::sendNotificationSync);
        }
        else
        {
            setValue (newValue, juce::sendNotificationSync);
        }

        return true;
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<Handler> (*this);
    }

private:
    juce::Label* findValueLabel() const
    {
        for (auto* child : getChildren())
            if (auto* label = dynamic_cast<juce::Label*> (child))
                return label;

        return nullptr;
    }

    // Enter or Escape in the value box puts the focus back on the knob. Losing focus some other way (Tab, a click
    // elsewhere) leaves it where the user sent it.
    void labelTextChanged (juce::Label*) override {}

    void editorHidden (juce::Label*, juce::TextEditor& editor) override
    {
        if (editor.hasKeyboardFocus (true))
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<KeyboardSlider> (this)]
                                             {
                                                 if (safe != nullptr && safe->isShowing())
                                                     safe->grabKeyboardFocus();
                                             });
    }

    bool step (int direction, double fractionOfTravel)
    {
        const double current = getValue();
        const double proportion = juce::jlimit (0.0, 1.0, valueToProportionOfLength (current) + (double) direction * fractionOfTravel);
        double target = proportionOfLengthToValue (proportion);

        // A stepped control moves by whole steps, never by nothing.
        const double interval = getInterval();

        if (interval > 0.0)
        {
            target = snapValue (target, DragMode::notDragging);

            if (juce::approximatelyEqual (target, current))
                target = juce::jlimit (getMinimum(), getMaximum(), current + (double) direction * interval);
        }

        return setByKey (target);
    }

    //==========================================================================
    class ValueInterface final : public juce::AccessibilityValueInterface
    {
    public:
        explicit ValueInterface (KeyboardSlider& s) : slider (s) {}

        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return slider.getValue(); }

        void setValue (double newValue) override { slider.setByKey (slider.snapValue (newValue, DragMode::notDragging)); }

        juce::String getCurrentValueAsString() const override { return slider.spokenText (slider.getValue()); }
        void setValueAsString (const juce::String& text) override { setValue (slider.getValueFromText (text)); }

        AccessibleValueRange getRange() const override
        {
            const double interval = slider.getInterval();
            return { { slider.getMinimum(), slider.getMaximum() },
                     interval > 0.0 ? interval : (slider.getMaximum() - slider.getMinimum()) * coarseStep };
        }

    private:
        KeyboardSlider& slider;
    };

    class Handler final : public juce::AccessibilityHandler
    {
    public:
        explicit Handler (KeyboardSlider& s)
            : juce::AccessibilityHandler (s, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                          Interfaces { std::make_unique<ValueInterface> (s) }),
              slider (s)
        {
        }

        juce::String getDescription() const override
        {
            return slider.describe != nullptr ? slider.describe() : juce::AccessibilityHandler::getDescription();
        }

        juce::String getHelp() const override { return {}; }

    private:
        KeyboardSlider& slider;
    };
};

} // namespace scratchpad::ui
