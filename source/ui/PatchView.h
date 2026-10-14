#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Knob.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace scratchpad::ui
{

/** The signal chain once a wave has been picked: oscillator -> gate -> output.
    Later milestones add cards in signal order; nothing is shown that is not
    in the audio path. */
class PatchView final : public juce::Component
{
public:
    explicit PatchView (plugin::ScratchpadProcessor&);
    ~PatchView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void syncWaveButtons (int waveIndex);

    plugin::ScratchpadProcessor& processor;

    class WaveButton;

    std::array<std::unique_ptr<WaveButton>, dsp::numWaves> waveButtons;
    Knob levelKnob;
    Knob volumeKnob;

    juce::ParameterAttachment waveAttachment;
    int currentWave = 2;

    juce::Rectangle<int> oscCard, gateNode, outputCard, waveWell, selector, firstLink, secondLink,
                         caption, listenPill;
    float chainTitleRight = 0.0f;
};

} // namespace scratchpad::ui
