#include "ui/PerformanceBar.h"

#include "ui/LookAndFeel.h"

namespace scratchpad::ui
{

namespace
{
    constexpr int minOctave = 1;
    constexpr int maxOctave = 7;
    constexpr int keyboardKeys = 24; // two octaves, plus the top C
    constexpr int whiteKeysShown = 15;

    struct KeyMap { char key; int semitone; };
    constexpr KeyMap keyMap[] = {
        { 'a', 0 }, { 'w', 1 }, { 's', 2 }, { 'e', 3 }, { 'd', 4 }, { 'f', 5 }, { 't', 6 },
        { 'g', 7 }, { 'y', 8 }, { 'h', 9 }, { 'u', 10 }, { 'j', 11 }, { 'k', 12 }
    };
}

PerformanceBar::PerformanceBar (plugin::ScratchpadProcessor& p)
    : processor (p)
{
    setTitle ("Keyboard");

    keyState.addListener (this);

    keyboard.setTitle ("On-screen keyboard");
    keyboard.setScrollButtonsVisible (false);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setMidiChannel (1);
    keyboard.setVelocity (velocity, false);
    keyboard.setOctaveForMiddleC (4); // note 60 is C4, so the label below matches the key names

    // Exactly the brief's mapping, rather than JUCE's longer default.
    keyboard.clearKeyMappings();
    for (const auto& m : keyMap)
        keyboard.setKeyPressForNote (juce::KeyPress ((int) m.key), m.semitone);
    keyboard.setKeyPressBaseOctave (octave);
    addAndMakeVisible (keyboard);

    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);

    for (auto* b : { &octaveDown, &octaveUp })
    {
        b->setPaintingIsUnclipped (true);
        b->getProperties().set (buttonProps::radius, 8.0);
        b->getProperties().set (buttonProps::fontSize, 14.0);
    }

    octaveDown.setTitle ("Octave down");
    octaveUp.setTitle ("Octave up");
    octaveDown.onClick = [this] { setOctave (octave - 1); };
    octaveUp.onClick = [this] { setOctave (octave + 1); };
    addAndMakeVisible (octaveDown);
    addAndMakeVisible (octaveUp);

    bendWheel.setSliderStyle (juce::Slider::LinearVertical);
    bendWheel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bendWheel.setRange (-1.0, 1.0, 0.0);
    bendWheel.setValue (0.0, juce::dontSendNotification);
    bendWheel.setDoubleClickReturnValue (true, 0.0);
    bendWheel.setTitle ("Pitch bend");
    bendWheel.setMouseDragSensitivity (80);
    bendWheel.onValueChange = [this] { sendBend ((float) bendWheel.getValue()); };
    bendWheel.onDragEnd = [this]
    {
        // Springs back to centre like a hardware wheel.
        bendWheel.setValue (0.0, juce::sendNotificationSync);
    };
    addAndMakeVisible (bendWheel);

    setOctave (octave);
}

PerformanceBar::~PerformanceBar()
{
    releaseAllNotes();
    keyState.removeListener (this);
}

void PerformanceBar::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel)
{
    plugin::UiEvent e;
    e.type = plugin::UiEvent::Type::noteOn;
    e.note = (std::uint8_t) juce::jlimit (0, 127, note);
    e.value = vel;
    processor.getUiEvents().push (e);
}

void PerformanceBar::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    plugin::UiEvent e;
    e.type = plugin::UiEvent::Type::noteOff;
    e.note = (std::uint8_t) juce::jlimit (0, 127, note);
    processor.getUiEvents().push (e);
}

void PerformanceBar::sendBend (float value)
{
    plugin::UiEvent e;
    e.type = plugin::UiEvent::Type::pitchBend;
    e.value = value;
    processor.getUiEvents().push (e);
}

void PerformanceBar::releaseAllNotes()
{
    // Tells the keyboard component to forget keys it thinks are down, then
    // releases whatever the shared state still has on.
    keyboard.focusLost (juce::Component::focusChangedDirectly);
    keyState.allNotesOff (1);
}

void PerformanceBar::setOctave (int newOctave)
{
    octave = juce::jlimit (minOctave, maxOctave, newOctave);

    releaseAllNotes();
    keyboard.setKeyPressBaseOctave (octave);
    keyboard.setAvailableRange (octave * 12, octave * 12 + keyboardKeys);
    keyboard.setLowestVisibleKey (octave * 12);

    octaveDown.setEnabled (octave > minOctave);
    octaveUp.setEnabled (octave < maxOctave);
    repaint (octaveLabelArea);
}

void PerformanceBar::setVelocity (float newVelocity)
{
    velocity = juce::jlimit (0.1f, 1.0f, newVelocity);
    keyboard.setVelocity (velocity, false);
    repaint (velocityLabelArea);
}

bool PerformanceBar::handleKeyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    switch (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()))
    {
        case 'z': setOctave (octave - 1); return true;
        case 'x': setOctave (octave + 1); return true;
        case 'c': setVelocity (velocity - 0.1f); return true;
        case 'v': setVelocity (velocity + 0.1f); return true;
        default:  break;
    }

    return keyboard.keyPressed (key);
}

bool PerformanceBar::handleKeyStateChanged (bool isDown)
{
    return keyboard.keyStateChanged (isDown);
}

void PerformanceBar::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    drawRaised (g, getLocalBounds().toFloat(), 18.0f, p, Elevation::raised, p.surface);

    g.setColour (p.ink);
    g.setFont (uiFont (13.0f));
    g.drawText ("Octave " + juce::String (octave - 1), octaveLabelArea, juce::Justification::centredLeft, false);
    g.drawText ("Velocity " + juce::String (juce::roundToInt (velocity * 100.0f)) + "%", velocityLabelArea,
                juce::Justification::centredLeft, false);

    g.setColour (p.mute);
    g.setFont (uiFont (12.0f));
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("A\xe2\x80\x93K play notes \xc2\xb7 W E T Y U are the black keys \xc2\xb7 Z / X octave \xc2\xb7 C / V velocity")),
                      hintArea, juce::Justification::centredLeft, 4);

    drawCaption (g, "Bend", bendCaptionArea.toFloat(), p.mute, juce::Justification::centred);
}

void PerformanceBar::resized()
{
    auto area = getLocalBounds().reduced (16, 12);

    // Octave and velocity on the left, then the bend wheel, the keys, and the key hints.
    auto left = area.removeFromLeft (150);
    auto row = left.removeFromTop (28);
    octaveDown.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (6);
    octaveUp.setBounds (row.removeFromLeft (28));
    row.removeFromLeft (10);
    octaveLabelArea = row;
    left.removeFromTop (10);
    velocityLabelArea = left.removeFromTop (20);

    area.removeFromLeft (12);
    auto bendArea = area.removeFromLeft (52);
    bendCaptionArea = bendArea.removeFromBottom (16);
    bendWheel.setBounds (bendArea);
    area.removeFromLeft (12);

    hintArea = area.removeFromRight (200);
    area.removeFromRight (12);

    keyboard.setKeyWidth ((float) area.getWidth() / (float) whiteKeysShown);
    keyboard.setBounds (area.withTrimmedBottom (4));
}

} // namespace scratchpad::ui
