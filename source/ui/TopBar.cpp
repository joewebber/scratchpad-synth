#include "ui/TopBar.h"

namespace scratchpad::ui
{

TopBar::TopBar (plugin::ScratchpadProcessor& p)
    : processor (p),
      meter ([&p] { return p.takeOutputPeak(); })
{
    setTitle ("Top bar");

    newPatchButton.onClick = [this] { processor.newPatch(); };

    undoButton.onClick = [this] { processor.undoManager.undo(); };
    redoButton.onClick = [this] { processor.undoManager.redo(); };

    settingsButton.setTitle ("Audio and MIDI settings");
    settingsButton.setPaintingIsUnclipped (true);
    settingsButton.onClick = [this]
    {
        if (processor.openAudioSettings)
            processor.openAudioSettings();
    };
    noDeviceButton.onClick = settingsButton.onClick;

    for (auto* b : std::initializer_list<juce::Component*> { &newPatchButton, &undoButton, &redoButton,
                                                              &settingsButton, &noDeviceButton })
        addChildComponent (b);

    newPatchButton.setVisible (true);
    undoButton.setVisible (true);
    redoButton.setVisible (true);
    settingsButton.setVisible (processor.openAudioSettings != nullptr);

    addAndMakeVisible (meter);

    processor.undoManager.addChangeListener (this);
    refreshHistory();

    if (processor.audioDeviceMissing)
        startTimerHz (2);
}

TopBar::~TopBar()
{
    processor.undoManager.removeChangeListener (this);
}

void TopBar::applyColours()
{
    noDeviceButton.setColour (juce::TextButton::buttonColourId, paletteOf (*this).accent);
    noDeviceButton.setGlyphColour (paletteOf (*this).onAccent);
    repaint();
}

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshHistory();
}

void TopBar::timerCallback()
{
    refreshAudioDeviceChip();
}

void TopBar::refreshAudioDeviceChip()
{
    const bool missing = processor.audioDeviceMissing && processor.audioDeviceMissing();

    if (noDeviceButton.isVisible() != missing)
    {
        noDeviceButton.setVisible (missing);
        resized();
    }
}

void TopBar::refreshHistory()
{
    auto& um = processor.undoManager;
    const auto undoName = um.getUndoDescription();

    lastStep = um.canUndo() ? (undoName.isEmpty() ? "Edit" : undoName) : "Nothing yet";
    undoButton.setEnabled (um.canUndo());
    redoButton.setEnabled (um.canRedo());
    undoButton.setTitle (um.canUndo() ? "Undo " + undoName : "Undo");
    redoButton.setTitle (um.canRedo() ? "Redo " + um.getRedoDescription() : "Redo");
    repaint (patchPill);
}

void TopBar::setPickerMode (bool shouldPick)
{
    picking = shouldPick;

    for (auto* b : { &newPatchButton, &undoButton, &redoButton })
        b->setVisible (! picking);

    resized();
    repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    auto bounds = getLocalBounds().toFloat();

    drawRaised (g, bounds, 16.0f, p, Elevation::raised, p.surface);

    // Logo: a circle with a small wave in it.
    const auto logo = juce::Rectangle<float> (32.0f, 32.0f).withCentre ({ 26.0f, bounds.getCentreY() });
    drawRaised (g, logo, 16.0f, p, Elevation::small, p.surface);

    auto wave = juce::Drawable::parseSVGPath ("M1.5 8c1.6-4.5 3.6-4.5 5 0s3.4 4.5 5 0 1.8-2.4 3-2");
    wave.applyTransform (juce::AffineTransform::translation (logo.getX() + 8.0f, logo.getY() + 8.0f));
    g.setColour (p.ink);
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto title = uiFont (14.0f, Weight::medium);
    title.setExtraKerningFactor (0.26f);
    g.setFont (title);
    g.drawText ("SCRATCHPAD", juce::Rectangle<float> (50.0f, 0.0f, titleRight - 50.0f, bounds.getHeight()),
                juce::Justification::centredLeft, false);

    if (picking)
    {
        g.setColour (p.track);
        g.fillRect (juce::Rectangle<float> (1.0f, 22.0f).withCentre ({ titleRight + 12.5f, bounds.getCentreY() }));
        drawCaption (g, "New patch", newPatchCaption.toFloat(), p.mute);
        return;
    }

    if (! patchPill.isEmpty())
    {
        const auto pill = patchPill.toFloat();
        drawInset (g, pill, 12.0f, p, p.well);

        auto text = pill.reduced (14.0f, 5.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (14.0f));
        g.drawText ("Untitled patch", text.removeFromTop (17.0f), juce::Justification::centredLeft, false);
        text.removeFromTop (1.0f);
        g.setColour (p.mute);
        g.setFont (uiFont (11.0f));
        g.drawText ("Last: " + lastStep, text.removeFromTop (13.0f), juce::Justification::centredLeft, true);
    }
}

void TopBar::resized()
{
    auto title = uiFont (14.0f, Weight::medium);
    title.setExtraKerningFactor (0.26f);
    titleRight = 50.0f + textWidth (title, "SCRATCHPAD");

    const int centreY = getHeight() / 2;
    auto row = [centreY] (int x, int w, int h) { return juce::Rectangle<int> (x, centreY - h / 2, w, h); };

    int x = (int) std::ceil (titleRight) + 12;

    if (picking)
    {
        newPatchCaption = row (x + 13, 120, 14);
        patchPill = {};
    }
    else
    {
        patchPill = row (x, 230, 41);
        x = patchPill.getRight() + 12;
        newPatchButton.setBounds (row (x, 32, 32));
        x += 32 + 12;
        undoButton.setBounds (row (x, 32, 32));
        x += 32 + 4;
        redoButton.setBounds (row (x, 32, 32));
    }

    int right = getWidth() - 12;

    if (settingsButton.isVisible())
    {
        settingsButton.setBounds (row (right - 84, 84, 32));
        right -= 84 + 12;
    }

    if (noDeviceButton.isVisible())
    {
        const int w = noDeviceButton.getIdealWidth();
        noDeviceButton.setBounds (row (right - w, w, 32));
        right -= w + 12;
    }

    meter.setBounds (row (right - 140, 140, 10));
}

} // namespace scratchpad::ui
