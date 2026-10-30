#include "plugin/PluginEditor.h"

namespace scratchpad::plugin
{

ScratchpadEditor::ScratchpadEditor (ScratchpadProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      topBar (p),
      patchView (p),
      performanceBar (p),
      explainPanel (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Scratchpad Synth");

    addAndMakeVisible (topBar);
    addAndMakeVisible (patchView);
    addAndMakeVisible (performanceBar);
    addAndMakeVisible (explainPanel);

    // After the children exist, so they all pick up the palette.
    setLookAndFeel (&lookAndFeel);

    setResizable (false, false);
    setSize (baseWidth, baseHeight);

    juce::Desktop::getInstance().addFocusChangeListener (this);
}

ScratchpadEditor::~ScratchpadEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    setLookAndFeel (nullptr);
}

void ScratchpadEditor::globalFocusChanged (juce::Component* focused)
{
    // Don't leave notes hanging if keyboard focus leaves the window while a
    // computer key is held (alt-tab, a dialog opening).
    if (focused == nullptr || (focused != this && ! isParentOf (focused)))
        performanceBar.releaseAllNotes();
}

bool ScratchpadEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && patchView.cancelConnecting())
        return true;

    return performanceBar.handleKeyPressed (key);
}

bool ScratchpadEditor::keyStateChanged (bool isKeyDown)
{
    return performanceBar.handleKeyStateChanged (isKeyDown);
}

void ScratchpadEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::paletteOf (*this).bg);
}

void ScratchpadEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    topBar.setBounds (area.removeFromTop (52));
    area.removeFromTop (10);

    explainPanel.setBounds (area.removeFromRight (ui::ExplainPanel::width));
    area.removeFromRight (10);

    constexpr int performanceHeight = 96;
    performanceBar.setBounds (area.removeFromBottom (performanceHeight));
    area.removeFromBottom (10);
    patchView.setBounds (area);
}

} // namespace scratchpad::plugin
