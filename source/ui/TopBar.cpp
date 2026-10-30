#include "ui/TopBar.h"

#include "plugin/PatchState.h"

namespace scratchpad::ui
{

TopBar::TopBar (plugin::ScratchpadProcessor& p)
    : processor (p),
      limiter ([&p] { return p.getLimiterReductionDb(); }),
      meter ([&p] { return p.takeOutputPeak(); })
{
    setTitle ("Top bar");
    setFocusContainerType (FocusContainerType::focusContainer);

    newPatchButton.onClick = [this]
    {
        processor.newPatch();
        a11y::announce ("New patch");
    };
    savePatchButton.onClick = [this] { savePatch(); };
    openPatchButton.onClick = [this] { openPatch(); };

    undoButton.onClick = [this] { undoOrRedo (true); };
    redoButton.onClick = [this] { undoOrRedo (false); };

    settingsButton.setTitle ("Audio and MIDI settings");
    settingsButton.setPaintingIsUnclipped (true);
    settingsButton.onClick = [this]
    {
        if (processor.openAudioSettings)
            processor.openAudioSettings();
    };
    noDeviceButton.onClick = settingsButton.onClick;

    newPatchButton.getProperties().set ("explainId", "control.newPatch");
    savePatchButton.getProperties().set ("explainId", "control.savePatch");
    openPatchButton.getProperties().set ("explainId", "control.openPatch");
    undoButton.getProperties().set ("explainId", "control.undo");
    redoButton.getProperties().set ("explainId", "control.redo");
    settingsButton.getProperties().set ("explainId", "control.settings");
    noDeviceButton.getProperties().set ("explainId", "control.settings");
    meter.getProperties().set ("explainId", "control.meter");
    viewButton.getProperties().set ("explainId", "control.viewMenu");

    for (auto* b : std::initializer_list<juce::Component*> { &newPatchButton, &savePatchButton, &openPatchButton,
                                                              &undoButton, &redoButton, &settingsButton, &noDeviceButton })
        addChildComponent (b);

    newPatchButton.setVisible (true);
    savePatchButton.setVisible (true);
    openPatchButton.setVisible (true);
    undoButton.setVisible (true);
    redoButton.setVisible (true);
    settingsButton.setVisible (processor.openAudioSettings != nullptr);

    addAndMakeVisible (limiter);
    addAndMakeVisible (viewButton);
    addAndMakeVisible (meter);
    addAndMakeVisible (patchRegion);

    // Tab, and the screen reader's order, left to right: the patch, its buttons, then the readouts and the buttons on the right.
    int order = 1;
    for (auto* c : std::initializer_list<juce::Component*> { &patchRegion, &newPatchButton, &savePatchButton, &openPatchButton,
                                                              &undoButton, &redoButton, &limiter, &meter, &noDeviceButton,
                                                              &settingsButton, &viewButton })
        c->setExplicitFocusOrder (order++);

    processor.undoManager.addChangeListener (this);
    processor.getPatchNameBroadcaster().addChangeListener (this);
    refreshHistory();

    if (processor.audioDeviceMissing)
        startTimerHz (2);
}

TopBar::~TopBar()
{
    processor.undoManager.removeChangeListener (this);
    processor.getPatchNameBroadcaster().removeChangeListener (this);
}

void TopBar::applyColours()
{
    noDeviceButton.setColour (juce::TextButton::buttonColourId, paletteOf (*this).accent);
    noDeviceButton.setGlyphColour (paletteOf (*this).onAccent);
    repaint();
}

void TopBar::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &processor.getPatchNameBroadcaster())
    {
        repaint (patchPill);
        refreshPatchRegion();
    }
    else
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
    refreshPatchRegion();
}

void TopBar::refreshPatchRegion()
{
    patchRegion.setText ("Patch: " + processor.getPatchName() + ". Last step: " + lastStep);
}

void TopBar::undoOrRedo (bool undo)
{
    auto& um = processor.undoManager;
    const auto what = undo ? um.getUndoDescription() : um.getRedoDescription();

    if (undo ? um.undo() : um.redo())
        a11y::announce ((undo ? "Undid " : "Redid ") + what);
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
    g.drawText ("SCRATCHPAD", juce::Rectangle<float> (50.0f, bounds.getCentreY() - 15.0f, titleRight - 50.0f, 17.0f),
                juce::Justification::centredLeft, false);

    g.setColour (p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String ("v") + SCRATCHPAD_VERSION,
                juce::Rectangle<float> (50.0f, bounds.getCentreY() + 3.0f, titleRight - 50.0f, 12.0f),
                juce::Justification::centredLeft, false);

    if (! patchPill.isEmpty())
    {
        const auto pill = patchPill.toFloat();
        drawInset (g, pill, 12.0f, p, p.well);

        auto text = pill.reduced (14.0f, 5.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (14.0f));
        g.drawText (processor.getPatchName(), text.removeFromTop (17.0f), juce::Justification::centredLeft, true);
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

    patchPill = row (x, 230, 41);
    patchRegion.setBounds (patchPill);
    x = patchPill.getRight() + 12;
    newPatchButton.setBounds (row (x, 32, 32));
    x += 32 + 4;
    savePatchButton.setBounds (row (x, 32, 32));
    x += 32 + 4;
    openPatchButton.setBounds (row (x, 32, 32));
    x += 32 + 12;
    undoButton.setBounds (row (x, 32, 32));
    x += 32 + 4;
    redoButton.setBounds (row (x, 32, 32));

    int right = getWidth() - 12;

    viewButton.setBounds (row (right - 32, 32, 32));
    right -= 32 + 12;

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
    limiter.setBounds (row (meter.getX() - 10 - 52, 52, 16));
}

void TopBar::savePatch()
{
    // The dialog needs somewhere real to start, so the patches folder is made the first time anyone saves.
    const auto patches = plugin::ScratchpadProcessor::getPatchesDirectory();
    (void) patches.createDirectory();
    const auto start = patches.getChildFile (processor.getPatchName() + patch::fileExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Save patch", start, patch::fileWildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto chosen = chooser.getResult();

                                  if (chosen == juce::File())
                                      return; // cancelled

                                  const auto file = patch::withFileExtension (chosen);

                                  if (processor.savePatch (file))
                                  {
                                      a11y::announce ("Saved " + processor.getPatchName());
                                      return;
                                  }

                                  a11y::announce ("Could not save the patch", juce::AccessibilityHandler::AnnouncementPriority::high);
                                  juce::AlertWindow::showMessageBoxAsync (
                                      juce::MessageBoxIconType::WarningIcon, "Could not save the patch",
                                      "Scratchpad could not write " + file.getFileName() + " to " + file.getParentDirectory().getFullPathName()
                                          + ". Check that the folder exists and that you are allowed to save there. Your patch is still open here, but it has not been saved.");
                              });
}

void TopBar::openPatch()
{
    const auto patches = plugin::ScratchpadProcessor::getPatchesDirectory();

    fileChooser = std::make_unique<juce::FileChooser> ("Open patch", patches.isDirectory() ? patches : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                       patch::fileWildcard);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();

                                  if (file == juce::File())
                                      return; // cancelled

                                  if (processor.loadPatch (file))
                                  {
                                      a11y::announce ("Opened " + processor.getPatchName());
                                      return;
                                  }

                                  a11y::announce ("Could not open the patch", juce::AccessibilityHandler::AnnouncementPriority::high);
                                  juce::AlertWindow::showMessageBoxAsync (
                                      juce::MessageBoxIconType::WarningIcon, "Could not open the patch",
                                      "Scratchpad could not open " + file.getFileName() + ". It may not be a Scratchpad patch, it may be damaged, or a newer version of Scratchpad may have saved it. Your current patch has not changed.");
                              });
}

bool TopBar::handleKeyPressed (const juce::KeyPress& key)
{
    const auto modifiers = key.getModifiers().getRawFlags() & juce::ModifierKeys::allKeyboardModifiers;
    const auto code = juce::CharacterFunctions::toLowerCase (key.getKeyCode());

    if (modifiers == (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier))
    {
        if (code != 'z')
            return false;

        undoOrRedo (false);
        return true;
    }

    if (modifiers != juce::ModifierKeys::commandModifier)
        return false;

    switch (code)
    {
        case 's': savePatch(); return true;
        case 'o': openPatch(); return true;
        case 'z': undoOrRedo (true); return true;
        case 'y': undoOrRedo (false); return true;
        default: return false;
    }
}

} // namespace scratchpad::ui
