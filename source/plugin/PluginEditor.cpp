#include "plugin/PluginEditor.h"

#include "plugin/Parameters.h"

namespace scratchpad::plugin
{

ScratchpadEditor::ScratchpadEditor (ScratchpadProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      topBar (p),
      patchView (p),
      performanceBar (p),
      presenceAttachment (*p.apvts.getParameter (params::osc1Enabled),
                          [this] (float v) { refreshMode (v >= 0.5f); },
                          &p.undoManager)
{
    setWantsKeyboardFocus (true);
    setTitle ("Scratchpad Synth");

    addAndMakeVisible (topBar);
    addChildComponent (picker);
    addChildComponent (patchView);
    addChildComponent (performanceBar);

    // After the children exist, so they all pick up the palette.
    setLookAndFeel (&lookAndFeel);

    picker.onPreview = [this] (int wave) { setPreviewWave (wave); };
    picker.onChoose = [this] (int wave)
    {
        setPreviewWave (-1);
        processor.pickWave (wave);
    };

    setResizable (false, false);
    setSize (baseWidth, baseHeight);

    juce::Desktop::getInstance().addFocusChangeListener (this);
    presenceAttachment.sendInitialUpdate();
}

ScratchpadEditor::~ScratchpadEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    setPreviewWave (-1);
    setLookAndFeel (nullptr);
}

void ScratchpadEditor::setPreviewWave (int wave)
{
    if (wave == previewingWave)
        return;

    UiEvent e;

    if (previewingWave >= 0)
    {
        e.type = UiEvent::Type::previewOff;
        e.note = (std::uint8_t) ui::WavePicker::previewNote;
        processor.getUiEvents().push (e);
    }

    previewingWave = wave;

    if (wave >= 0)
    {
        e.type = UiEvent::Type::previewOn;
        e.note = (std::uint8_t) ui::WavePicker::previewNote;
        e.wave = (std::uint8_t) wave;
        processor.getUiEvents().push (e);
    }
}

void ScratchpadEditor::refreshMode (bool started)
{
    if (! started)
        performanceBar.releaseAllNotes();

    picker.setVisible (! started);
    patchView.setVisible (started);
    performanceBar.setVisible (started);
    topBar.setPickerMode (! started);

    // Panels paint their shadows outside their bounds; clear what the hidden one left.
    repaint();

    if (started)
        grabKeyboardFocus();
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
    return patchView.isVisible() && performanceBar.handleKeyPressed (key);
}

bool ScratchpadEditor::keyStateChanged (bool isKeyDown)
{
    return patchView.isVisible() && performanceBar.handleKeyStateChanged (isKeyDown);
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

    picker.setBounds (area);

    constexpr int performanceHeight = 170;
    performanceBar.setBounds (area.removeFromBottom (performanceHeight));
    area.removeFromBottom (10);
    patchView.setBounds (area);
}

} // namespace scratchpad::plugin
