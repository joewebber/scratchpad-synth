#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/Keyboard.h"
#include "ui/KeyboardSlider.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** The pitch wheel. With the mouse it springs back to the middle when let go (the bar does that on drag end). With
    the keyboard, Up and Down (and Page Up and Down) bend it while the key is held, and it springs back when the
    key is let go or the focus leaves; Home, End and Delete put it in the middle at once. */
class PitchWheel final : public KeyboardSlider
{
public:
    PitchWheel() { steps = { 0.125, 0.025, 0.25 }; }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const int code = key.getKeyCode();

        if (! key.getModifiers().isAnyModifierKeyDown()
            && (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey
                || code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey))
        {
            setByKey (0.0);
            return true;
        }

        return KeyboardSlider::keyPressed (key);
    }

    bool keyStateChanged (bool) override
    {
        using juce::KeyPress;

        const bool bending = KeyPress::isKeyCurrentlyDown (KeyPress::upKey) || KeyPress::isKeyCurrentlyDown (KeyPress::downKey)
                          || KeyPress::isKeyCurrentlyDown (KeyPress::leftKey) || KeyPress::isKeyCurrentlyDown (KeyPress::rightKey)
                          || KeyPress::isKeyCurrentlyDown (KeyPress::pageUpKey) || KeyPress::isKeyCurrentlyDown (KeyPress::pageDownKey);

        if (! bending && ! isMouseButtonDown())
            setByKey (0.0);

        return false;
    }

    void focusLost (FocusChangeType type) override
    {
        KeyboardSlider::focusLost (type);
        setByKey (0.0);
    }

protected:
    bool keyChangeIsGesture() const override { return false; }
};

/** Two-octave on-screen keyboard, pitch bend and mod wheels, octave shift and the
    computer-keyboard mapping (A to K white keys, W E T Y U black keys, Z/X
    octave down/up, C/V velocity down/up).

    Everything leaves through the processor's lock-free UI queue. The editor
    forwards its key events to `handleKeyPressed` and `handleKeyStateChanged`
    so notes play whichever child has focus.

    The on-screen keys are played with those computer keys, which work from anywhere in the window except while a
    value is being typed, so the keys themselves are not a Tab stop. Tab visits the octave buttons, then the pitch
    wheel and the mod wheel (Up and Down arrows move them). To a screen reader the bar is a group; its octave and
    velocity are one line of text, and the keyboard is described with how to play it. Octave and velocity changes
    are announced. */
class PerformanceBar final : public juce::Component,
                             private juce::MidiKeyboardState::Listener
{
public:
    explicit PerformanceBar (plugin::ScratchpadProcessor&);
    ~PerformanceBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool handleKeyPressed (const juce::KeyPress&);
    bool handleKeyStateChanged (bool isDown);

    /** Releases every note this bar started, including computer-keyboard keys. */
    void releaseAllNotes();

    int getOctave() const noexcept { return octave; }
    float getVelocity() const noexcept { return velocity; }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return a11y::makeGroupHandler (*this); }

    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float vel) override;

    void setOctave (int newOctave, bool announceIt = true);
    void setVelocity (float newVelocity);
    void sendBend (float value);
    void sendModWheel (float value);
    juce::String summaryText() const;

    plugin::ScratchpadProcessor& processor;

    juce::MidiKeyboardState keyState;
    Keyboard keyboard { keyState, juce::MidiKeyboardComponent::horizontalKeyboard };

    juce::TextButton octaveDown { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) };
    juce::TextButton octaveUp { "+" };
    PitchWheel bendWheel;
    KeyboardSlider modWheel;
    a11y::Region summary;

    juce::Rectangle<int> octaveLabelArea, velocityLabelArea, hintArea, bendCaptionArea, modCaptionArea;

    int octave = 4; // JUCE octave numbering: the A key plays note 12 * octave
    float velocity = 0.8f;
};

} // namespace scratchpad::ui
