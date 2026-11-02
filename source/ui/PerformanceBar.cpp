#include "ui/PerformanceBar.h"

#include "ui/LookAndFeel.h"

namespace scratchpad::ui
{

namespace
{
    constexpr int minOctave = 1;
    constexpr int maxOctaveCap = 7; // the highest octave the buttons reach when the keyboard is short enough
    constexpr int compactOctaves = 2, maxOctavesShown = 9; // 9 octaves (12 * 9 - 1 keys from C1) still ends inside the MIDI range
    constexpr int whiteKeysPerOctave = 7, whiteKeyWidth = 27, keyboardHeight = 70;

    /** The last key's offset from the first: C to B of `octaves` octaves. */
    constexpr int keyboardKeys (int octaves) { return 12 * octaves - 1; }

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
        return octavesDescription() + " Play it with the A to K keys on your computer keyboard: "
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
    addAndMakeVisible (voicing);

    // Tab: the octave buttons, then the wheels. The text and the keyboard follow in the screen reader's order.
    octaveDown.setExplicitFocusOrder (1);
    octaveUp.setExplicitFocusOrder (2);
    summary.setExplicitFocusOrder (3);
    bendWheel.setExplicitFocusOrder (4);
    modWheel.setExplicitFocusOrder (5);
    keyboard.setExplicitFocusOrder (6);

    setOctave (octave, false);
    lastBend = processor.getPitchBend();
    lastMod = processor.getModWheel();
    bendWheel.setValue ((double) lastBend, juce::dontSendNotification);
    modWheel.setValue ((double) lastMod, juce::dontSendNotification);
    startTimerHz (30);
}

juce::String PerformanceBar::summaryText() const
{
    return "Octave " + juce::String (octave - 1) + ", velocity " + juce::String (juce::roundToInt (velocity * 100.0f)) + " percent, "
           + juce::String (octavesShown) + " octaves";
}

void PerformanceBar::timerCallback()
{
    // Hardware wheels reach the engine as MIDI; follow them on screen. A wheel being held by the user is left alone.
    const auto follow = [] (juce::Slider& wheel, float now, float& last)
    {
        if (now == last)
            return;

        last = now;

        if (! wheel.isMouseButtonDown() && ! wheel.hasKeyboardFocus (false))
            wheel.setValue ((double) now, juce::dontSendNotification);
    };

    follow (bendWheel, processor.getPitchBend(), lastBend);
    follow (modWheel, processor.getModWheel(), lastMod);
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

int PerformanceBar::maxOctave() const noexcept
{
    // The highest octave whose last key (12 * octave + 12 * octavesShown - 1) is still a MIDI note.
    return juce::jlimit (minOctave, maxOctaveCap, (127 + 1 - 12 * octavesShown) / 12);
}

juce::String PerformanceBar::octavesDescription() const
{
    static const char* const words[] = { "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine", "Ten" };
    const auto count = octavesShown < (int) std::size (words) ? juce::String (words[octavesShown]) : juce::String (octavesShown);
    return count + " octaves from C" + juce::String (octave - 1) + ".";
}

void PerformanceBar::applyKeyboardRange()
{
    // Whatever is held is let go through the key state, so the processor gets its note-offs before the keys move.
    releaseAllNotes();
    keyboard.setKeyPressBaseOctave (octave);
    keyboard.setAvailableRange (octave * 12, octave * 12 + keyboardKeys (octavesShown));
    keyboard.setLowestVisibleKey (octave * 12);

    octaveDown.setEnabled (octave > minOctave);
    octaveUp.setEnabled (octave < maxOctave());
    summary.setText (summaryText());
    repaint (octaveLabelArea);
    repaint (octavesLabelArea);
}

void PerformanceBar::setOctave (int newOctave, bool announceIt)
{
    octave = juce::jlimit (minOctave, maxOctave(), newOctave);
    applyKeyboardRange();

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

    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::small, p.surface);

    g.setColour (p.line);
    g.fillRect (divider);

    g.setColour (p.ink);
    g.setFont (uiFont (13.0f));
    g.drawText ("C" + juce::String (octave - 1), octaveLabelArea, juce::Justification::centred, false);

    g.setColour (p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText ("Velocity " + juce::String (juce::roundToInt (velocity * 100.0f)) + "%", velocityLabelArea, juce::Justification::centred, false);
    g.drawText (juce::String (octavesShown) + " octaves", octavesLabelArea, juce::Justification::centred, false);

    drawCaption (g, "Bend", bendCaptionArea.toFloat().expanded (10.0f, 0.0f), p.mute, juce::Justification::centred);
    drawCaption (g, "Mod", modCaptionArea.toFloat().expanded (10.0f, 0.0f), p.mute, juce::Justification::centred);
}

void PerformanceBar::resized()
{
    // The design's flex row, resolved to pixels at 100%: each group is centred on the card's height. The extra height
    // over the compact card goes to the keys and the wheels' slots.
    const int h = getHeight();
    const int extraHeight = juce::jmax (0, h - layout::voicingKeyboard.h);
    const auto centred = [h] (int x, int width, int height) { return juce::Rectangle<int> (x, (h - height) / 2, width, height); };

    voicing.setBounds (centred (layout::padding::voicing + 4, VoicingCard::width, VoicingCard::height));

    divider = centred (voicing.getRight() + 16, 1, 56);

    // Bend and mod wheels: a 16 x 50 slot each, 22 px wide thumb, the caption under.
    const auto wheels = centred (divider.getRight() + 19, 60, 50 + extraHeight + 4 + 13);
    auto bendArea = wheels.withWidth (26), modArea = wheels.withTrimmedLeft (34);
    bendCaptionArea = bendArea.removeFromBottom (13);
    modCaptionArea = modArea.removeFromBottom (13);
    bendWheel.setBounds (bendArea.withTrimmedBottom (4));
    modWheel.setBounds (modArea.withTrimmedBottom (4));

    // Octave: [-] C3 [+] over the velocity.
    const auto octaveBlock = centred (wheels.getRight() + 20, 74, 22 + 5 + 13 + 3 + 13);
    auto row = octaveBlock.withHeight (22);
    octaveDown.setBounds (row.removeFromLeft (22));
    octaveUp.setBounds (row.removeFromRight (22));
    octaveLabelArea = row;
    velocityLabelArea = octaveBlock.withTrimmedTop (27).withHeight (13);
    octavesLabelArea = octaveBlock.withTrimmedTop (43).withHeight (13);
    summary.setBounds (octaveBlock);

    // The keyboard starts where it does in the compact card and takes the whole octaves that fit before the right padding.
    const int rightInset = layout::padding::voicing + 4;
    const int keyboardX = layout::voicingKeyboard.w - rightInset - compactOctaves * whiteKeysPerOctave * whiteKeyWidth;
    const int fitting = (getWidth() - rightInset - keyboardX) / (whiteKeysPerOctave * whiteKeyWidth);
    const int newOctaves = juce::jlimit (compactOctaves, maxOctavesShown, fitting);

    keyboard.setKeyWidth ((float) whiteKeyWidth);
    keyboard.setBounds (centred (keyboardX, newOctaves * whiteKeysPerOctave * whiteKeyWidth, keyboardHeight + extraHeight));
    for (auto* child : keyboard.getChildren())
        if (dynamic_cast<juce::Button*> (child) != nullptr)
            child->setAccessible (false);

    if (newOctaves != octavesShown)
    {
        octavesShown = newOctaves;
        octave = juce::jlimit (minOctave, maxOctave(), octave); // setOctave announces nothing here
        applyKeyboardRange();
    }
}

} // namespace scratchpad::ui
