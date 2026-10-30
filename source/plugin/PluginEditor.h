#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ExplainPanel.h"
#include "ui/LookAndFeel.h"
#include "ui/PatchView.h"
#include "ui/PerformanceBar.h"
#include "ui/TopBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace scratchpad::plugin
{

/** One window, 1500 x 900: the design's 1200 x 760 plus room for the Explain panel, the wider cards and (M5) the row
    of effects under the signal chain. Below the top bar sit the signal chain over the keyboard, with the Explain
    panel to their right. */
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

    ScratchpadProcessor& processor;

    ui::ScratchpadLookAndFeel lookAndFeel { ui::darkPalette() };

    ui::TopBar topBar;
    ui::PatchView patchView;
    ui::PerformanceBar performanceBar;
    ui::ExplainPanel explainPanel;
};

} // namespace scratchpad::plugin
