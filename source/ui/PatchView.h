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
#include "ui/SourcesCard.h"
#include "ui/Layout.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <optional>

namespace scratchpad::ui
{

/** The synth once a wave has been picked, laid out on the stage's fixed boxes (`ui/Layout.h`; nothing here sizes
    itself from content). Under the "Signal chain" heading is the per-voice zone: the Sources card (the oscillator
    table and noise), the Filter with the Filter Envelope under it, and the Amp with the Amp Envelope under it, the
    signal running left to right through two chevrons. Under the "Effects" heading, the voices are added together
    (the sum disc), pass through the four effect places in the order the processor keeps them, and end in Out. Under
    "Modulation": two LFOs, the Playing card (velocity, mod wheel, aftertouch, key) and the connections list.

    Modules the user has not added show as dashed slots in their place; "+ Add module" and the slots add them, and a
    card's More menu removes it. Presence follows the modules' `enabled` parameters, so undo, redo and loading a
    session re-lay everything out too.

    The effects row has four equal places. Present effects fill them from the left in `getEffectOrder()` order; the
    effects that are not in the patch show their dashed slots after them. Dragging an effect's grip (or pressing the
    arrow keys on it) reorders through `moveEffect` only: the cards follow the processor's order, which is also what
    undo, loading and a new patch change.

    A transparent overlay on top draws the cable while a connection is being made and owns the arcs around modulated
    knobs.

    Keyboard and screen reader: Tab visits the chain in reading order (see `setFocusOrders`): the "Add module"
    button, then the sources, the filter and its envelope, the amp and its envelope, the effects left to right, the
    output, then the modulation (the LFOs, the Playing card, the connections). Adding a module from the keyboard puts
    the focus on the new card's first control; removing one puts it on the dashed slot that takes its place. Module,
    connection and effect-order changes are announced. */
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

    /** How a taller window's extra height is shared out (`layout::Grow`). The cards are handed their grown boxes and lay
        out from their own height; the gaps between them stay as they are. The view is as much taller as the zone,
        the effects and the modulation row take together (the keyboard card is not part of it). */
    void setGrow (const layout::Grow&);

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
    void showAddMenu();
    void setFocusOrders();
    void updateListenRegion();
    ModuleCard* cardFor (plugin::Module);
    SlotButton* slotFor (plugin::Module);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return a11y::makeGroupHandler (*this); }
    juce::String listenHint() const;
    bool present (plugin::Module m) const { return processor.isModulePresent (m); }
    plugin::Presence presence() const;
    void statusChanged() override { repaint (local (layout::listenPill)); updateListenRegion(); }
    void modulationChanged() override { repaint (local (layout::listenPill)); updateListenRegion(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    EffectSlots effectSlots() const;
    EffectCard& effectCard (engine::EffectKind kind) { return effectCardStorage[(size_t) kind]; }
    SlotButton& effectSlot (engine::EffectKind kind) { return effectSlotStorage[(size_t) kind]; }
    juce::Rectangle<int> effectPlace (int index) const;
    juce::Rectangle<int> effectsRow() const;
    int effectIndexAt (int x) const;
    void beginEffectDrag (engine::EffectKind, const juce::MouseEvent&);
    void dragEffect (const juce::MouseEvent&);
    void endEffectDrag();
    void moveEffectTo (engine::EffectKind, int destinationIndex);
    void nudgeEffect (engine::EffectKind, int direction);

    plugin::ScratchpadProcessor& processor;
    ModulationModel model { processor };

    SourcesCard sourcesCard { processor, model };
    FilterCard filterCard { processor, model };
    EnvelopeCard filterEnvCard { processor, model, plugin::Module::filterEnv };
    AmpCard ampCard { processor, model };
    EnvelopeCard ampEnvCard { processor, model, plugin::Module::ampEnv };
    OutputCard outputCard { processor };

    std::array<EffectCard, engine::numEffects> effectCardStorage {{
        { processor, engine::EffectKind::distortion }, { processor, engine::EffectKind::chorus },
        { processor, engine::EffectKind::delay }, { processor, engine::EffectKind::reverb }
    }};

    LfoCard lfo1Card { processor, model, 0 }, lfo2Card { processor, model, 1 };
    PlayingCard playingCard { processor, model };
    ConnectionsPanel connectionsPanel { model };

    SlotButton filterSlot, filterEnvSlot, ampEnvSlot, lfo1Slot, lfo2Slot;
    std::array<SlotButton, engine::numEffects> effectSlotStorage;
    juce::TextButton addButton { "+ Add module" };

    ModOverlay overlay { model };

    a11y::Region listenRegion, perVoiceRegion, effectsRegion;

    std::array<std::unique_ptr<juce::ParameterAttachment>, plugin::allModules.size()> presenceWatchers;
    std::array<bool, plugin::allModules.size()> lastPresence {};
    std::optional<plugin::Module> focusNewModule; // set while the UI adds a module, so its card takes the focus

    EffectDrag effectDrag;

    /** `box` (a design region, in stage pixels) as this view's own coordinates, grown by the extra height. */
    juce::Rectangle<int> local (const layout::Box& box) const noexcept;

    layout::Grow grow;
};

} // namespace scratchpad::ui
