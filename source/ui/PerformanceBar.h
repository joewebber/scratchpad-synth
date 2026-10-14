#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Keyboard.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** Two-octave on-screen keyboard, pitch bend wheel, octave shift and the
    computer-keyboard mapping (A to K white keys, W E T Y U black keys, Z/X
    octave down/up, C/V velocity down/up).

    Everything leaves through the processor's lock-free UI queue. The editor
    forwards its key events to `handleKeyPressed` and `handleKeyStateChanged`
    so notes play whichever child has focus. */
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
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float vel) override;

    void setOctave (int newOctave);
    void setVelocity (float newVelocity);
    void sendBend (float value);

    plugin::ScratchpadProcessor& processor;

    juce::MidiKeyboardState keyState;
    Keyboard keyboard { keyState, juce::MidiKeyboardComponent::horizontalKeyboard };

    juce::TextButton octaveDown { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) };
    juce::TextButton octaveUp { "+" };
    juce::Slider bendWheel;

    juce::Rectangle<int> octaveLabelArea, velocityLabelArea, hintArea, bendCaptionArea;

    int octave = 4; // JUCE octave numbering: the A key plays note 12 * octave
    float velocity = 0.8f;
};

} // namespace scratchpad::ui
