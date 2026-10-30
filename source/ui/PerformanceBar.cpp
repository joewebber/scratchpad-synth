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
    setTitle ("Performance bar");
    setFocusContainerType (FocusContainerType::focusContainer);

    keyState.addListener (this);

    keyboard.setTitle ("On-screen keyboard");
    keyboard.describe = [this]
    {
        return "Two octaves from C" + juce::String (octave - 1) + ". Play it with the A to K keys on your computer keyboard: "
               "A S D F G H J K are the white keys and W E T Y U the black ones. Z and X change octave, C and V change velocity.";
    };
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
    // JUCE keeps its hidden scroll buttons as children even when the scroll controls are disabled. They are
    // implementation details, not controls in this editor's accessibility tree.
    for (auto* child : keyboard.getChildren())
        if (dynamic_cast<juce::Button*> (child) != nullptr)
            child->setAccessible (false);
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
    octaveDown.setDescription ("Moves the keyboard down an octave. The Z key does the same.");
    octaveUp.setDescription ("Moves the keyboard up an octave. The X key does the same.");
    octaveDown.onClick = [this] { setOctave (octave - 1); };
    octaveUp.onClick = [this] { setOctave (octave + 1); };
    addAndMakeVisible (octaveDown);
    addAndMakeVisible (octaveUp);
    octaveDown.getProperties().set ("explainId", "control.octave");
    octaveUp.getProperties().set ("explainId", "control.octave");
    keyboard.getProperties().set ("explainId", "control.keyboard");

    bendWheel.setSliderStyle (juce::Slider::LinearVertical);
    bendWheel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    bendWheel.setRange (-1.0, 1.0, 0.0);
    bendWheel.setValue (0.0, juce::dontSendNotification);
    bendWheel.setDoubleClickReturnValue (true, 0.0);
    bendWheel.setTitle ("Pitch bend");
    bendWheel.describe = [] { return juce::String ("Bends the pitch of the notes you play. It springs back to the middle when you let go. The Up and Down arrow keys bend it while you hold them."); };
    bendWheel.textFromValueFunction = [] (double v)
    {
        return std::abs (v) < 0.005 ? juce::String ("centre") : juce::String (juce::roundToInt (std::abs (v) * 100.0)) + " percent " + (v > 0.0 ? "up" : "down");
    };
    bendWheel.valueFromTextFunction = [] (const juce::String& text)
    {
        return (text.containsIgnoreCase ("down") ? -1.0 : 1.0) * text.getDoubleValue() * 0.01;
    };
    bendWheel.setMouseDragSensitivity (80);
    bendWheel.onValueChange = [this] { sendBend ((float) bendWheel.getValue()); };
    bendWheel.onDragEnd = [this]
    {
        // Springs back to centre like a hardware wheel.
        bendWheel.setValue (0.0, juce::sendNotificationSync);
    };
    addAndMakeVisible (bendWheel);
    bendWheel.getProperties().set ("explainId", "control.bend");

    // The mod wheel stays where it is left, like a real one.
    modWheel.setSliderStyle (juce::Slider::LinearVertical);
    modWheel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    modWheel.setRange (0.0, 1.0, 0.0);
    modWheel.setValue (0.0, juce::dontSendNotification);
    modWheel.setDoubleClickReturnValue (true, 0.0);
    modWheel.setTitle ("Mod wheel");
    modWheel.describe = [] { return juce::String ("Moves everything you have connected to the mod wheel. It stays where you leave it. The Up and Down arrow keys move it."); };
    modWheel.steps = { 0.05, 0.01, 0.25 };
    modWheel.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    modWheel.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue() * 0.01; };
    modWheel.setMouseDragSensitivity (80);
    modWheel.onValueChange = [this] { sendModWheel ((float) modWheel.getValue()); };
    addAndMakeVisible (modWheel);
    modWheel.getProperties().set ("explainId", "control.modWheel");

    addAndMakeVisible (summary);

    // Tab: the octave buttons, then the wheels. The text and the keyboard follow in the screen reader's order.
    octaveDown.setExplicitFocusOrder (1);
    octaveUp.setExplicitFocusOrder (2);
    summary.setExplicitFocusOrder (3);
    bendWheel.setExplicitFocusOrder (4);
    modWheel.setExplicitFocusOrder (5);
    keyboard.setExplicitFocusOrder (6);

    setOctave (octave, false);
}

juce::String PerformanceBar::summaryText() const
{
    return "Octave " + juce::String (octave - 1) + ", velocity " + juce::String (juce::roundToInt (velocity * 100.0f)) + " percent";
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

void PerformanceBar::sendModWheel (float value)
{
    plugin::UiEvent e;
    e.type = plugin::UiEvent::Type::modWheel;
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

void PerformanceBar::setOctave (int newOctave, bool announceIt)
{
    octave = juce::jlimit (minOctave, maxOctave, newOctave);

    releaseAllNotes();
    keyboard.setKeyPressBaseOctave (octave);
    keyboard.setAvailableRange (octave * 12, octave * 12 + keyboardKeys);
    keyboard.setLowestVisibleKey (octave * 12);

    octaveDown.setEnabled (octave > minOctave);
    octaveUp.setEnabled (octave < maxOctave);
    summary.setText (summaryText());
    repaint (octaveLabelArea);

    if (announceIt)
        a11y::announce ("Octave " + juce::String (octave - 1));
}

void PerformanceBar::setVelocity (float newVelocity)
{
    velocity = juce::jlimit (0.1f, 1.0f, newVelocity);
    keyboard.setVelocity (velocity, false);
    summary.setText (summaryText());
    repaint (velocityLabelArea);
    a11y::announce ("Velocity " + juce::String (juce::roundToInt (velocity * 100.0f)) + " percent");
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
    drawCaption (g, "Mod", modCaptionArea.toFloat(), p.mute, juce::Justification::centred);
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
    summary.setBounds (octaveLabelArea.getUnion (velocityLabelArea));

    area.removeFromLeft (12);
    auto wheels = area.removeFromLeft (76);
    auto bendArea = wheels.removeFromLeft (38);
    auto modArea = wheels;
    bendCaptionArea = bendArea.removeFromBottom (16);
    modCaptionArea = modArea.removeFromBottom (16);
    bendWheel.setBounds (bendArea);
    modWheel.setBounds (modArea);
    area.removeFromLeft (12);

    hintArea = area.removeFromRight (200);
    area.removeFromRight (12);

    keyboard.setKeyWidth ((float) area.getWidth() / (float) whiteKeysShown);
    keyboard.setBounds (area.withTrimmedBottom (4));
    for (auto* child : keyboard.getChildren())
        if (dynamic_cast<juce::Button*> (child) != nullptr)
            child->setAccessible (false);
}

} // namespace scratchpad::ui
