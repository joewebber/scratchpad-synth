#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ExplainPanel.h"
#include "ui/LookAndFeel.h"
#include "ui/PatchView.h"
#include "ui/PerformanceBar.h"
#include "ui/TopBar.h"
#include "ui/WavePicker.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace scratchpad::plugin
{

/** One window, 1500 x 900: the design's 1200 x 760 plus room for the Explain panel, the wider cards and (M5) the row
    of effects under the signal chain. Until a wave is picked the whole window below the
    top bar is the wave picker; after that it is the signal chain over the
    keyboard. Which one shows follows the `osc1.enabled` parameter, so New
    patch, undo and loading a session all land in the right place. */
class ScratchpadEditor final : public juce::AudioProcessorEditor,
                               private juce::FocusChangeListener
{
public:
    static constexpr int baseWidth = 1500;
    static constexpr int baseHeight = 900;

    explicit ScratchpadEditor (ScratchpadProcessor&);
    ~ScratchpadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void globalFocusChanged (juce::Component*) override;
    void refreshMode (bool started);
    void setPreviewWave (int wave);

    ScratchpadProcessor& processor;

    ui::ScratchpadLookAndFeel lookAndFeel { ui::darkPalette() };

    ui::TopBar topBar;
    ui::WavePicker picker;
    ui::PatchView patchView;
    ui::PerformanceBar performanceBar;
    ui::ExplainPanel explainPanel;

    juce::ParameterAttachment presenceAttachment;
    int previewingWave = -1;
};

} // namespace scratchpad::plugin
