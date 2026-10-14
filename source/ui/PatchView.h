#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModuleCards.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace scratchpad::ui
{

/** The synth once a wave has been picked: the signal chain panel (sources ->
    filter -> amp -> output, with the envelopes under the stage they shape)
    over a voicing panel (poly / mono / legato, voices, glide, bend range).

    Modules the user has not added show as dashed slots; "+ Add module" and the
    slots add them, the cards' remove buttons take them away. Presence follows
    the modules' `enabled` parameters, so undo, redo and loading a session
    re-lay the chain out too. */
class PatchView final : public juce::Component
{
public:
    explicit PatchView (plugin::ScratchpadProcessor&);
    ~PatchView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshModules();
    void showAddMenu();
    juce::String listenHint() const;
    bool present (plugin::Module m) const { return processor.isModulePresent (m); }

    plugin::ScratchpadProcessor& processor;

    OscillatorCard osc1Card { processor, 0 }, osc2Card { processor, 1 }, osc3Card { processor, 2 };
    NoiseCard noiseCard { processor };
    FilterCard filterCard { processor };
    EnvelopeCard filterEnvCard { processor, plugin::Module::filterEnv };
    AmpCard ampCard { processor };
    EnvelopeCard ampEnvCard { processor, plugin::Module::ampEnv };
    OutputCard outputCard { processor };

    SlotButton osc2Slot, osc3Slot, noiseSlot, filterSlot, filterEnvSlot, ampEnvSlot;
    juce::TextButton addButton { "+ Add module" };

    ChoiceSegments voiceMode;
    Knob voices, glide, bendRange;
    juce::ParameterAttachment voiceModeWatcher;
    int voiceModeIndex = 0;

    std::array<std::unique_ptr<juce::ParameterAttachment>, plugin::allModules.size()> presenceWatchers;

    juce::Rectangle<int> chainPanel, voicingPanel, well, titleRow, listenPill, voicingText;
    std::array<juce::Rectangle<int>, 3> chevrons;
    int voicingTitleRight = 0;
    float chainTitleRight = 0.0f;
};

} // namespace scratchpad::ui
