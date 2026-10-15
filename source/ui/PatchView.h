#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModOverlay.h"
#include "ui/ModuleCards.h"
#include "ui/ModulationModel.h"
#include "ui/ModulationStrip.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace scratchpad::ui
{

/** The synth once a wave has been picked. Above, the signal chain: sources -> filter -> amp ->
    output, with the envelopes under the stage they shape and the voicing under the amp.
    Below, the modulation strip: two LFOs, the other sources' handles, and the eight connection slots.

    Modules the user has not added show as dashed slots; "+ Add module" and the slots add them,
    the cards' remove buttons take them away. Presence follows the modules' `enabled`
    parameters, so undo, redo and loading a session re-lay everything out too.

    A transparent overlay on top draws the cable while a connection is being made and owns the
    arcs around modulated knobs. */
class PatchView final : public juce::Component,
                        private ModulationModel::Listener
{
public:
    explicit PatchView (plugin::ScratchpadProcessor&);
    ~PatchView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    /** Puts down a handle that is being held, if one is. True if there was one. */
    bool cancelConnecting();

private:
    void refreshModules();
    void showAddMenu();
    juce::String listenHint() const;
    bool present (plugin::Module m) const { return processor.isModulePresent (m); }
    void statusChanged() override { repaint (listenPill); }
    void modulationChanged() override { repaint (listenPill); }

    plugin::ScratchpadProcessor& processor;
    ModulationModel model { processor };

    OscillatorCard osc1Card { processor, model, 0 }, osc2Card { processor, model, 1 }, osc3Card { processor, model, 2 };
    NoiseCard noiseCard { processor, model };
    FilterCard filterCard { processor, model };
    EnvelopeCard filterEnvCard { processor, model, plugin::Module::filterEnv };
    AmpCard ampCard { processor, model };
    EnvelopeCard ampEnvCard { processor, model, plugin::Module::ampEnv };
    VoicingCard voicingCard { processor };
    OutputCard outputCard { processor };

    LfoCard lfo1Card { processor, model, 0 }, lfo2Card { processor, model, 1 };
    SourcesPanel sourcesPanel { processor, model };
    ConnectionsPanel connectionsPanel { model };

    SlotButton osc2Slot, osc3Slot, noiseSlot, filterSlot, filterEnvSlot, ampEnvSlot, lfo1Slot, lfo2Slot;
    juce::TextButton addButton { "+ Add module" };

    ModOverlay overlay { model };

    std::array<std::unique_ptr<juce::ParameterAttachment>, plugin::allModules.size()> presenceWatchers;

    juce::Rectangle<int> chainPanel, stripPanel, well, titleRow, listenPill;
    std::array<juce::Rectangle<int>, 3> chevrons;
    float chainTitleRight = 0.0f;
};

} // namespace scratchpad::ui
