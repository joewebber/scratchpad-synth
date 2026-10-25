#pragma once

#include "plugin/PluginProcessor.h"
#include "plugin/Suggestion.h"
#include "ui/ChoiceSegments.h"
#include "ui/EffectCards.h"
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
    output, with the envelopes under the stage they shape and the voicing under the amp, and under that the
    effects row (the global part of the chain: distortion, chorus, delay and reverb, in the order the processor
    keeps them). Below, the modulation strip: two LFOs, the other sources' handles, and the eight connection slots.

    Modules the user has not added show as dashed slots; "+ Add module" and the slots add them,
    the cards' remove buttons take them away. Presence follows the modules' `enabled`
    parameters, so undo, redo and loading a session re-lay everything out too.

    The effects row has four equal places. Present effects fill them from the left in `getEffectOrder()` order;
    the effects that are not in the patch show their dashed slots after them. Dragging an effect's grip (or
    pressing the arrow keys on it) reorders through `moveEffect` only: the cards follow the processor's order,
    which is also what undo, loading and a new patch change.

    An oscillator's chevron opens its extras (`OscillatorExtras`) as a drawer over the cards under it. One drawer
    is open at a time; it is the editor's own state and is never saved.

    A transparent overlay on top draws the cable while a connection is being made and owns the
    arcs around modulated knobs. */
class PatchView final : public juce::Component,
                        private ModulationModel::Listener,
                        private juce::ChangeListener
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
    /** Effects in the present / absent split the effects row is laid out from. */
    struct EffectSlots
    {
        std::array<engine::EffectKind, engine::numEffects> present {}, absent {};
        int numPresent = 0, numAbsent = 0;
    };

    struct EffectDrag
    {
        bool active = false;
        engine::EffectKind kind = engine::EffectKind::distortion;
        int from = 0, to = 0;   // indices into the present effects
        int grabOffset = 0;     // pointer x minus the card's x when the drag began
    };

    void refreshModules();
    void refreshExtras();
    void toggleExtras (int oscillator);
    void showAddMenu();
    juce::String listenHint() const;
    bool present (plugin::Module m) const { return processor.isModulePresent (m); }
    plugin::Presence presence() const;
    void statusChanged() override { repaint (listenPill); }
    void modulationChanged() override { repaint (listenPill); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    EffectSlots effectSlots() const;
    EffectCard& effectCard (engine::EffectKind kind) { return effectCardStorage[(size_t) kind]; }
    SlotButton& effectSlot (engine::EffectKind kind) { return effectSlotStorage[(size_t) kind]; }
    juce::Rectangle<int> effectPlace (int index) const;
    int effectIndexAt (int x) const;
    void beginEffectDrag (engine::EffectKind, const juce::MouseEvent&);
    void dragEffect (const juce::MouseEvent&);
    void endEffectDrag();
    void moveEffectTo (engine::EffectKind, int destinationIndex);
    void nudgeEffect (engine::EffectKind, int direction);

    plugin::ScratchpadProcessor& processor;
    ModulationModel model { processor };

    OscillatorCard osc1Card { processor, model, 0 }, osc2Card { processor, model, 1 }, osc3Card { processor, model, 2 };
    OscillatorExtras osc1Extras { processor, 0 }, osc2Extras { processor, 1 }, osc3Extras { processor, 2 };
    NoiseCard noiseCard { processor, model };
    FilterCard filterCard { processor, model };
    EnvelopeCard filterEnvCard { processor, model, plugin::Module::filterEnv };
    AmpCard ampCard { processor, model };
    EnvelopeCard ampEnvCard { processor, model, plugin::Module::ampEnv };
    VoicingCard voicingCard { processor };
    OutputCard outputCard { processor };

    std::array<EffectCard, engine::numEffects> effectCardStorage {{
        { processor, engine::EffectKind::distortion }, { processor, engine::EffectKind::chorus },
        { processor, engine::EffectKind::delay }, { processor, engine::EffectKind::reverb }
    }};

    LfoCard lfo1Card { processor, model, 0 }, lfo2Card { processor, model, 1 };
    SourcesPanel sourcesPanel { processor, model };
    ConnectionsPanel connectionsPanel { model };

    SlotButton osc2Slot, osc3Slot, noiseSlot, filterSlot, filterEnvSlot, ampEnvSlot, lfo1Slot, lfo2Slot;
    std::array<SlotButton, engine::numEffects> effectSlotStorage;
    juce::TextButton addButton { "+ Add module" };

    ModOverlay overlay { model };

    std::array<std::unique_ptr<juce::ParameterAttachment>, plugin::allModules.size()> presenceWatchers;

    int openExtras = -1; // the oscillator whose drawer is open, or -1
    EffectDrag effectDrag;

    juce::Rectangle<int> chainPanel, stripPanel, well, titleRow, listenPill, effectsCaption, effectsRow;
    std::array<juce::Rectangle<int>, 3> chevrons;
    float chainTitleRight = 0.0f;
};

} // namespace scratchpad::ui
