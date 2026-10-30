#pragma once

#include "plugin/PluginProcessor.h"
#include "plugin/Suggestion.h"
#include "ui/Accessibility.h"
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
#include <optional>

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
    arcs around modulated knobs.

    Keyboard and screen reader: Tab visits the chain in reading order (see `setFocusOrders`): the "Add module"
    button, then the sources top to bottom, the filter and its envelope, the amp, its envelope and the voicing, the
    output, the effects left to right, then the modulation strip (the LFOs, the other sources, the eight
    connection slots). An open drawer follows its oscillator. Adding a module from the keyboard puts the focus on
    the new card's first control; removing one puts it on the dashed slot that takes its place. Module, connection
    and effect-order changes are announced. */
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

    /** Where the keyboard goes to add a module: used by the slots and the menu, so the new card gets the focus. */
    void addModuleFromUi (plugin::Module);

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
    void closeExtras (int oscillator);
    void showAddMenu();
    void setFocusOrders();
    void updateListenRegion();
    ModuleCard* cardFor (plugin::Module);
    SlotButton* slotFor (plugin::Module);
    OscillatorCard& oscillatorCard (int index) { return index == 0 ? osc1Card : index == 1 ? osc2Card : osc3Card; }
    OscillatorExtras& oscillatorExtras (int index) { return index == 0 ? osc1Extras : index == 1 ? osc2Extras : osc3Extras; }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return a11y::makeGroupHandler (*this); }
    juce::String listenHint() const;
    bool present (plugin::Module m) const { return processor.isModulePresent (m); }
    plugin::Presence presence() const;
    void statusChanged() override { repaint (listenPill); updateListenRegion(); }
    void modulationChanged() override { repaint (listenPill); updateListenRegion(); }
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

    a11y::Region listenRegion, perVoiceRegion, effectsRegion;

    std::array<std::unique_ptr<juce::ParameterAttachment>, plugin::allModules.size()> presenceWatchers;
    std::array<bool, plugin::allModules.size()> lastPresence {};
    std::optional<plugin::Module> focusNewModule; // set while the UI adds a module, so its card takes the focus

    int openExtras = -1; // the oscillator whose drawer is open, or -1
    EffectDrag effectDrag;

    juce::Rectangle<int> chainPanel, stripPanel, well, titleRow, listenPill, effectsCaption, effectsRow;
    std::array<juce::Rectangle<int>, 3> chevrons;
    float chainTitleRight = 0.0f;
};

} // namespace scratchpad::ui
