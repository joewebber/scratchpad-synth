#include "ui/PatchView.h"

#include "plugin/Parameters.h"
#include "plugin/Suggestion.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

using plugin::Module;

namespace
{
    namespace L = layout;

    juce::Font headingFont()
    {
        auto font = uiFont (12.0f, Weight::medium);
        font.setExtraKerningFactor (cssTracking (0.24f));
        return font;
    }

    /** A chevron between two columns: the signal runs left to right. */
    void drawChevron (juce::Graphics& g, const Palette& p, juce::Rectangle<int> link)
    {
        const float cx = (float) link.getCentreX(), cy = (float) link.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 2.0f, cy - 4.0f);
        chevron.lineTo (cx + 2.0f, cy);
        chevron.lineTo (cx - 2.0f, cy + 4.0f);
        g.setColour (p.mute);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** "EFFECTS" in the heading's letter-spaced capitals, then a muted one-line hint 14 px after it. */
    void drawSectionHeading (juce::Graphics& g, const Palette& p, juce::Rectangle<int> row, const juce::String& title, const juce::String& hint)
    {
        const auto font = headingFont();
        auto area = row.toFloat().reduced ((float) L::headingTextInset, 0.0f);
        const float titleWidth = textWidth (font, title.toUpperCase()) + 4.0f;

        g.setFont (font);
        g.setColour (p.ink);
        g.drawText (title.toUpperCase(), area.removeFromLeft (titleWidth), juce::Justification::centredLeft, false);
        area.removeFromLeft ((float) L::headingGap);

        g.setColour (p.mute);
        g.setFont (uiFont (12.0f));
        g.drawText (hint, area, juce::Justification::centredLeft, true);
    }

    const char* presenceId (Module which)
    {
        switch (which)
        {
            case Module::osc2:      return params::osc[1].enabled;
            case Module::osc3:      return params::osc[2].enabled;
            case Module::noise:     return params::noiseEnabled;
            case Module::filter:    return params::filterEnabled;
            case Module::ampEnv:    return params::ampEnv.enabled;
            case Module::filterEnv: return params::filterEnv.enabled;
            case Module::lfo1:      return params::lfo[0].enabled;
            case Module::lfo2:      return params::lfo[1].enabled;
            case Module::distortion: return params::distortionEnabled;
            case Module::chorus:     return params::chorusEnabled;
            case Module::delay:      return params::delayEnabled;
            case Module::reverb:     return params::reverbEnabled;
        }
        return params::osc[1].enabled;
    }

    /** The suggested module in the Add menu: its name over the one-sentence reason. */
    class SuggestedMenuItem final : public juce::PopupMenu::CustomComponent
    {
    public:
        SuggestedMenuItem (juce::String nameText, juce::String reasonText)
            : CustomComponent (true), name (std::move (nameText)), reason (std::move (reasonText)) {}

        void getIdealSize (int& width, int& height) override
        {
            width = 280;
            juce::AttributedString text;
            text.append (reason, uiFont (12.0f));
            juce::TextLayout layout;
            layout.createLayout (text, (float) width - 24.0f);
            height = 30 + (int) std::ceil (layout.getHeight());
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = paletteOf (*this);
            auto area = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
            g.setColour (isItemHighlighted() ? p.accent : p.accentSoft);
            g.fillRoundedRectangle (area, 8.0f);

            const auto ink = isItemHighlighted() ? p.onAccent : p.ink;
            area = area.reduced (10.0f, 4.0f);
            g.setColour (ink);
            g.setFont (uiFont (13.0f, Weight::medium));
            g.drawText (name, area.removeFromTop (20.0f), juce::Justification::centredLeft, true);
            g.setFont (uiFont (12.0f));
            g.drawFittedText (reason, area.toNearestInt(), juce::Justification::topLeft, 4);
        }

    private:
        juce::String name, reason;
    };
}

//==============================================================================
juce::Rectangle<int> PatchView::local (const layout::Box& box) const noexcept
{
    return box.rect().translated (-plan.patchView.x, -plan.patchView.y);
}

void PatchView::setPlan (const layout::Plan& newPlan)
{
    plan = newPlan;
    resized();
    repaint();
}

PatchView::PatchView (plugin::ScratchpadProcessor& p)
    : processor (p),
      filterSlot ("Add Filter", "Filter", SlotButton::Style::slot, "Cut or boost parts of the sound"),
      filterEnvSlot ("Add Filter Envelope", "Filter Env", SlotButton::Style::slot, "Open and close the filter over each note"),
      ampEnvSlot ("Add Amp Envelope", "Amp Env", SlotButton::Style::slot, "Shape how each note starts and ends"),
      lfo1Slot ("Add LFO 1", "LFO 1", SlotButton::Style::slot, "A slow wobble for vibrato and sweeps"),
      lfo2Slot ("Add LFO 2", "LFO 2", SlotButton::Style::slot, "A second, independent wobble"),
      effectSlotStorage {{
          { "Add Distortion", "Distortion", SlotButton::Style::slot, "Crunch and grit: squash the wave" },
          { "Add Chorus", "Chorus", SlotButton::Style::slot, "Thicken the sound with a slow swirl" },
          { "Add Delay", "Delay", SlotButton::Style::slot, "Echoes that repeat and fade away" },
          { "Add Reverb", "Reverb", SlotButton::Style::slot, "The sound of a room or a hall" }
      }}
{
    setTitle ("Signal chain");
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);
    setFocusContainerType (FocusContainerType::focusContainer); // so a screen reader nests the cards under "Signal chain"

    addButton.setTitle ("Add module");
    addButton.setPaintingIsUnclipped (true);
    addButton.getProperties().set (buttonProps::radius, 11.0);
    addButton.onClick = [this] { showAddMenu(); };
    addButton.setDescription ("Opens a list of the modules you can add: sources, a filter, envelopes, LFOs and effects.");

    addButton.getProperties().set ("explainId", "control.addModule");
    for (auto* slot : { &filterSlot, &filterEnvSlot, &ampEnvSlot, &lfo1Slot, &lfo2Slot })
        slot->getProperties().set ("explainId", "control.addModule");
    for (auto& slot : effectSlotStorage)
        slot.getProperties().set ("explainId", "control.addModule");

    const struct { SlotButton* slot; Module which; } slots[] = {
        { &filterSlot, Module::filter }, { &filterEnvSlot, Module::filterEnv }, { &ampEnvSlot, Module::ampEnv },
        { &lfo1Slot, Module::lfo1 }, { &lfo2Slot, Module::lfo2 }
    };
    for (const auto& s : slots)
    {
        const auto which = s.which;
        s.slot->onClick = [this, which] { addModuleFromUi (which); };
        addChildComponent (*s.slot);
    }

    for (size_t i = 0; i < effectSlotStorage.size(); ++i)
    {
        const auto which = effectModule ((engine::EffectKind) i);
        effectSlotStorage[i].onClick = [this, which] { addModuleFromUi (which); };
        addChildComponent (effectSlotStorage[i]);
    }

    sourcesCard.onAddModule = [this] (Module which) { addModuleFromUi (which); };

    for (auto* c : std::initializer_list<juce::Component*> { &sourcesCard, &filterCard, &filterEnvCard, &ampCard, &ampEnvCard,
                                                              &outputCard, &lfo1Card, &lfo2Card })
        addChildComponent (c);

    sourcesCard.setVisible (true);
    ampCard.setVisible (true);
    outputCard.setVisible (true);

    // Effect cards come before the overlay, so it stays on top.
    for (auto& card : effectCardStorage)
    {
        addChildComponent (card);

        const auto kind = card.getKind();
        auto& grip = card.getGrip();
        grip.onDragStart = [this, kind] (const juce::MouseEvent& e) { beginEffectDrag (kind, e); };
        grip.onDragMove = [this] (const juce::MouseEvent& e) { dragEffect (e); };
        grip.onDragEnd = [this] (const juce::MouseEvent&) { endEffectDrag(); };
        grip.onNudge = [this, kind] (int direction) { nudgeEffect (kind, direction); };
    }

    addAndMakeVisible (addButton);
    addAndMakeVisible (playingCard);
    addAndMakeVisible (connectionsPanel);
    addAndMakeVisible (overlay); // last, so it is on top
    addAndMakeVisible (listenRegion);
    addAndMakeVisible (perVoiceRegion);
    addAndMakeVisible (effectsRegion);

    perVoiceRegion.setText ("Per voice: every note you hold gets its own copy of this section");
    effectsRegion.setText ("Effects. Global: the voices are added together, then pass through these from left to right. "
                           "Press the left or right arrow key on an effect's reorder handle to change the order.");
    setFocusOrders();

    for (size_t i = 0; i < plugin::allModules.size(); ++i)
        presenceWatchers[i] = std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (presenceId (plugin::allModules[i])), [this] (float) { refreshModules(); });

    processor.getEffectsBroadcaster().addChangeListener (this);

    model.addListener (this);
    for (const auto m : plugin::allModules)
        lastPresence[(size_t) m] = present (m);

    refreshModules();
    updateListenRegion();
}

PatchView::~PatchView()
{
    processor.getEffectsBroadcaster().removeChangeListener (this);
    model.removeListener (this);
}

void PatchView::visibilityChanged()
{
    model.setActive (isVisible());
}

bool PatchView::cancelConnecting()
{
    if (! model.isConnecting())
        return false;

    model.cancelConnecting();
    return true;
}

juce::String PatchView::listenHint() const
{
    if (model.countConnections() > 0)
        return "Hold a key. The arc around a knob is how far it can move; the dot is where it is now.";
    if (present (Module::lfo1) || present (Module::lfo2))
        return "Drag the round handle of an LFO onto a knob, or click the handle and then a knob.";
    if (present (Module::filterEnv))
        return "Hold a key. The filter opens and closes as the envelope moves.";
    if (present (Module::filter))
        return "Hold a key and sweep the cutoff: the sound gets darker, then brighter.";
    if (present (Module::ampEnv))
        return "Hold a key. Attack sets how fast it fades in, Release how long it lingers.";
    if (present (Module::osc2) || present (Module::osc3) || present (Module::noise))
        return "Hold a key. Two sources that are slightly apart make the sound shimmer.";

    return "Hold a key. The note slams on, and stops dead when you let go.";
}

void PatchView::showAddMenu()
{
    juce::PopupMenu menu;
    const auto suggestion = plugin::suggestNext (presence());

    if (suggestion)
    {
        const auto index = (int) suggestion->module;
        menu.addSectionHeader ("Suggested next");
        menu.addCustomItem (index + 1, std::make_unique<SuggestedMenuItem> (plugin::ScratchpadProcessor::moduleName (suggestion->module),
                                                                           suggestion->reason));
    }

    const struct { const char* heading; std::initializer_list<Module> modules; } groups[] = {
        { "Sources", { Module::osc2, Module::osc3, Module::noise } },
        { "Shape", { Module::filter, Module::ampEnv, Module::filterEnv } },
        { "Movement", { Module::lfo1, Module::lfo2 } },
        { "Effects", { Module::distortion, Module::chorus, Module::delay, Module::reverb } }
    };

    for (const auto& group : groups)
    {
        bool headed = false;
        for (const auto m : group.modules)
        {
            if (present (m) || (suggestion && suggestion->module == m))
                continue;

            if (! std::exchange (headed, true))
                menu.addSectionHeader (group.heading);

            menu.addItem ((int) m + 1, plugin::ScratchpadProcessor::moduleName (m));
        }
    }

    juce::Component::SafePointer<PatchView> safe (this);
    showStyledMenu (menu, juce::PopupMenu::Options().withTargetComponent (&addButton).withMinimumWidth (addButton.getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result >= 1 && result <= (int) plugin::allModules.size())
                                safe->addModuleFromUi (plugin::allModules[(size_t) result - 1]);
                        });
}

plugin::Presence PatchView::presence() const
{
    plugin::Presence result {};
    for (const auto m : plugin::allModules)
        result[(size_t) m] = present (m);
    return result;
}

void PatchView::setFocusOrders()
{
    // Reading order, which is also the Tab order. A card and the dashed slot that stands in for it share a number
    // (only one of them shows at a time); things with the same number go top to bottom, then left to right.
    int order = 1;
    const auto next = [&order] (std::initializer_list<juce::Component*> group)
    {
        for (auto* c : group)
            c->setExplicitFocusOrder (order);

        ++order;
    };

    next ({ &addButton });
    next ({ &listenRegion });
    next ({ &perVoiceRegion });
    next ({ &sourcesCard });
    next ({ &filterCard, &filterSlot });
    next ({ &filterEnvCard, &filterEnvSlot });
    next ({ &ampCard });
    next ({ &ampEnvCard, &ampEnvSlot });
    next ({ &effectsRegion });
    next ({ &effectCardStorage[0], &effectCardStorage[1], &effectCardStorage[2], &effectCardStorage[3],
            &effectSlotStorage[0], &effectSlotStorage[1], &effectSlotStorage[2], &effectSlotStorage[3] });
    next ({ &outputCard });
    next ({ &lfo1Card, &lfo1Slot });
    next ({ &lfo2Card, &lfo2Slot });
    next ({ &playingCard });
    next ({ &connectionsPanel });
    next ({ &overlay });
}

ModuleCard* PatchView::cardFor (Module m)
{
    switch (m)
    {
        case Module::osc2:
        case Module::osc3:
        case Module::noise:      return nullptr; // rows of the Sources card, which hands out its own focus
        case Module::filter:     return &filterCard;
        case Module::ampEnv:     return &ampEnvCard;
        case Module::filterEnv:  return &filterEnvCard;
        case Module::lfo1:       return &lfo1Card;
        case Module::lfo2:       return &lfo2Card;
        case Module::distortion: return &effectCardStorage[(size_t) engine::EffectKind::distortion];
        case Module::chorus:     return &effectCardStorage[(size_t) engine::EffectKind::chorus];
        case Module::delay:      return &effectCardStorage[(size_t) engine::EffectKind::delay];
        case Module::reverb:     return &effectCardStorage[(size_t) engine::EffectKind::reverb];
    }

    return nullptr;
}

SlotButton* PatchView::slotFor (Module m)
{
    switch (m)
    {
        case Module::osc2:
        case Module::osc3:
        case Module::noise:      return nullptr;
        case Module::filter:     return &filterSlot;
        case Module::ampEnv:     return &ampEnvSlot;
        case Module::filterEnv:  return &filterEnvSlot;
        case Module::lfo1:       return &lfo1Slot;
        case Module::lfo2:       return &lfo2Slot;
        case Module::distortion: return &effectSlotStorage[(size_t) engine::EffectKind::distortion];
        case Module::chorus:     return &effectSlotStorage[(size_t) engine::EffectKind::chorus];
        case Module::delay:      return &effectSlotStorage[(size_t) engine::EffectKind::delay];
        case Module::reverb:     return &effectSlotStorage[(size_t) engine::EffectKind::reverb];
    }

    return nullptr;
}

void PatchView::addModuleFromUi (Module which)
{
    focusNewModule = which;
    processor.addModule (which);
    focusNewModule.reset();
}

void PatchView::updateListenRegion()
{
    listenRegion.setText (model.getStatus().isNotEmpty() ? model.getStatus() : "Listen for: " + listenHint());
}

void PatchView::refreshModules()
{
    // Where the keyboard is now, before cards come and go: a card that goes takes its focus with it.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    const bool hasFilter = present (Module::filter), hasFilterEnv = present (Module::filterEnv),
               hasAmpEnv = present (Module::ampEnv), hasLfo1 = present (Module::lfo1), hasLfo2 = present (Module::lfo2);

    sourcesCard.refreshPresence();
    filterCard.setVisible (hasFilter);
    filterSlot.setVisible (! hasFilter);
    filterEnvCard.setVisible (hasFilterEnv);
    filterEnvSlot.setVisible (! hasFilterEnv);
    ampEnvCard.setVisible (hasAmpEnv);
    ampEnvSlot.setVisible (! hasAmpEnv);
    lfo1Card.setVisible (hasLfo1);
    lfo1Slot.setVisible (! hasLfo1);
    lfo2Card.setVisible (hasLfo2);
    lfo2Slot.setVisible (! hasLfo2);

    for (int i = 0; i < engine::numEffects; ++i)
    {
        const bool isPresent = present (effectModule ((engine::EffectKind) i));
        effectCardStorage[(size_t) i].setVisible (isPresent);
        effectSlotStorage[(size_t) i].setVisible (! isPresent);
    }

    // The palette's advice: the dashed slot of the suggested module, if it has one on screen.
    const auto suggestion = plugin::suggestNext (presence());
    const struct { SlotButton* slot; Module which; } suggestable[] = {
        { &filterSlot, Module::filter }, { &filterEnvSlot, Module::filterEnv }, { &ampEnvSlot, Module::ampEnv },
        { &lfo1Slot, Module::lfo1 }, { &lfo2Slot, Module::lfo2 },
        { &effectSlotStorage[0], Module::distortion }, { &effectSlotStorage[1], Module::chorus },
        { &effectSlotStorage[2], Module::delay }, { &effectSlotStorage[3], Module::reverb }
    };
    for (const auto& s : suggestable)
        s.slot->setSuggested (suggestion && suggestion->module == s.which, suggestion ? suggestion->reason : "");

    for (const auto m : { Module::osc2, Module::osc3, Module::noise })
        if (auto* slot = dynamic_cast<SlotButton*> (sourcesCard.slotFor (m)))
            slot->setSuggested (suggestion && suggestion->module == m, suggestion ? suggestion->reason : "");

    // A handle that was armed may belong to a module that has just gone.
    if (model.isConnecting() && ! processor.isModSourcePresent (model.getConnectingSource()))
        model.cancelConnecting();

    bool everything = true;
    for (const auto m : plugin::allModules)
        everything = everything && present (m);
    addButton.setEnabled (! everything);

    resized();
    updateListenRegion();

    // What changed: one module added or taken away is said aloud, and the keyboard follows it. Several at once (a
    // patch opened, undone or started again) are the patch's news, not each module's.
    std::vector<Module> added, removed;
    for (const auto m : plugin::allModules)
    {
        const bool now = present (m);

        if (now != lastPresence[(size_t) m])
            (now ? added : removed).push_back (m);

        lastPresence[(size_t) m] = now;
    }

    if (added.size() == 1 && removed.empty())
        a11y::announce (plugin::ScratchpadProcessor::moduleName (added.front()) + " added");
    else if (removed.size() == 1 && added.empty())
        a11y::announce (plugin::ScratchpadProcessor::moduleName (removed.front()) + " removed");

    const auto isSource = [] (Module m) { return m == Module::osc2 || m == Module::osc3 || m == Module::noise; };

    // A module the user just added offers its first control. A module that goes while the keyboard was inside it
    // leaves the dashed slot that takes its place.
    if (focusNewModule.has_value() && added.size() == 1 && added.front() == *focusNewModule)
    {
        if (isSource (*focusNewModule))
            sourcesCard.focusModule (*focusNewModule);
        else if (auto* card = cardFor (*focusNewModule))
            card->focusFirstControl();
    }
    else if (focused != nullptr && removed.size() == 1 && juce::Component::getCurrentlyFocusedComponent() == nullptr)
    {
        const auto gone = removed.front();

        if (isSource (gone))
        {
            if (sourcesCard.isParentOf (focused))
                sourcesCard.focusSlot (gone);
        }
        else if (auto* card = cardFor (gone))
        {
            if (card->isParentOf (focused) || card == focused)
                if (auto* slot = slotFor (gone))
                    slot->grabKeyboardFocus();
        }
    }

    // Cards paint their shadows outside their bounds; clear what a removed card left behind.
    if (auto* parent = getParentComponent())
        parent->repaint();
    repaint();

    // The button that was just clicked may have been hidden; keep the computer keyboard playing.
    if (getCurrentlyFocusedComponent() == nullptr)
        if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
            if (editor->isShowing())
                editor->grabKeyboardFocus();
}

//==============================================================================
void PatchView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The processor's effect order changed (a move, undo, a loaded session or a new patch).
    resized();
    repaint();
}

PatchView::EffectSlots PatchView::effectSlots() const
{
    EffectSlots result;

    for (const auto kind : processor.getEffectOrder())
    {
        if (present (effectModule (kind)))
            result.present[(size_t) result.numPresent++] = kind;
        else
            result.absent[(size_t) result.numAbsent++] = kind;
    }

    return result;
}

juce::Rectangle<int> PatchView::effectPlace (int index) const
{
    return local (plan.fx[index]);
}

int PatchView::effectIndexAt (int x) const
{
    return juce::jlimit (0, engine::numEffects - 1, (x - local (plan.fx[0]).getX()) / (plan.fx[1].x - plan.fx[0].x));
}

void PatchView::beginEffectDrag (engine::EffectKind kind, const juce::MouseEvent& e)
{
    const auto slots = effectSlots();

    for (int i = 0; i < slots.numPresent; ++i)
    {
        if (slots.present[(size_t) i] != kind)
            continue;

        auto& card = effectCard (kind);
        effectDrag = { true, kind, i, i, e.getEventRelativeTo (this).x - card.getX() };
        card.toBehind (&overlay); // above the other cards, below the overlay
        repaint (effectsRow().expanded (12));
        return;
    }
}

void PatchView::dragEffect (const juce::MouseEvent& e)
{
    if (! effectDrag.active)
        return;

    const auto slots = effectSlots();
    auto& card = effectCard (effectDrag.kind);
    const int x = juce::jlimit (effectsRow().getX(), effectsRow().getRight() - card.getWidth(),
                                e.getEventRelativeTo (this).x - effectDrag.grabOffset);

    card.setTopLeftPosition (x, effectsRow().getY());
    effectDrag.to = juce::jlimit (0, juce::jmax (0, slots.numPresent - 1), effectIndexAt (x + card.getWidth() / 2));
    repaint (effectsRow().expanded (12));
}

void PatchView::endEffectDrag()
{
    if (! effectDrag.active)
        return;

    const auto drag = effectDrag;
    effectDrag = {};

    const auto slots = effectSlots();
    if (drag.to != drag.from && drag.to < slots.numPresent && slots.present[(size_t) drag.from] == drag.kind)
        moveEffectTo (drag.kind, drag.to);

    // Either the processor's new order, or back where it was.
    resized();
    repaint();
}

void PatchView::moveEffectTo (engine::EffectKind kind, int destinationIndex)
{
    const auto slots = effectSlots();
    const auto order = processor.getEffectOrder();

    // `moveEffect` speaks in positions of the full four-entry order, which includes effects that are not in the patch.
    const auto positionOf = [&order] (engine::EffectKind k) { return (int) (std::find (order.begin(), order.end(), k) - order.begin()); };
    processor.moveEffect (positionOf (kind), positionOf (slots.present[(size_t) destinationIndex]));

    const auto after = effectSlots();
    for (int i = 0; i < after.numPresent; ++i)
        if (after.present[(size_t) i] == kind)
            a11y::announce (plugin::ScratchpadProcessor::moduleName (effectModule (kind)) + " moved to position "
                            + juce::String (i + 1) + " of " + juce::String (after.numPresent));
}

void PatchView::nudgeEffect (engine::EffectKind kind, int direction)
{
    const auto slots = effectSlots();

    for (int i = 0; i < slots.numPresent; ++i)
    {
        if (slots.present[(size_t) i] != kind)
            continue;

        const int target = i + direction;
        if (target >= 0 && target < slots.numPresent)
        {
            moveEffectTo (kind, target);
            resized();
            repaint();
        }
        return;
    }
}

//==============================================================================
void PatchView::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    // Signal chain heading: the title, the Listen-for pill, and (a button) + Add module.
    {
        const auto heading = local (plan.signalChainHeading);
        const auto font = headingFont();
        g.setFont (font);
        g.setColour (p.ink);
        g.drawText ("SIGNAL CHAIN", heading.toFloat().reduced ((float) L::headingTextInset, 0.0f), juce::Justification::centredLeft, false);

        const auto pill = local (plan.listenPill).toFloat();
        const bool hasStatus = model.getStatus().isNotEmpty();
        g.setColour (hasStatus ? p.accent.withAlpha (0.9f) : p.accentSoft);
        g.fillRoundedRectangle (pill, 11.0f);

        auto text = pill.reduced (12.0f, 0.0f);

        if (! hasStatus)
        {
            auto captionFont = uiFont (11.0f);
            captionFont.setExtraKerningFactor (0.14f);
            drawCaption (g, "Listen for", text.removeFromLeft (textWidth (captionFont, "LISTEN FOR") + 2.0f), p.ink2);
            text.removeFromLeft (8.0f);
        }

        g.setColour (hasStatus ? p.onAccent : p.ink);
        g.setFont (uiFont (12.0f));
        g.drawText (hasStatus ? model.getStatus() : listenHint(), text, juce::Justification::centredLeft, true);
    }

    // The zone the per-voice modules sit in: a flat tint, with its caption running up the left strip.
    {
        const auto zone = local (plan.perVoiceZone);
        g.setColour (p.zone);
        g.fillRoundedRectangle (zone.toFloat(), 16.0f);

        const auto strip = local (plan.perVoiceCaption);
        auto captionFont = uiFont (11.0f);
        captionFont.setExtraKerningFactor (0.14f);

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) strip.getCentreX(), (float) strip.getCentreY()));
        g.setFont (captionFont);
        g.setColour (p.mute);
        const auto length = (float) strip.getHeight();
        g.drawText (juce::String ("PER VOICE ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " ONE COPY PER NOTE",
                    juce::Rectangle<float> ((float) strip.getCentreX() - length * 0.5f, (float) strip.getCentreY() - 9.0f, length, 18.0f),
                    juce::Justification::centred, false);
        g.restoreState();
    }

    drawChevron (g, p, local (plan.sourcesArrow));
    drawChevron (g, p, local (plan.filterAmpArrow));

    // Effects: the heading, the sum disc where the voices are added together, and where a dragged effect would land.
    drawSectionHeading (g, p, local (plan.effectsHeading), "Effects",
                        "Whole mix: the voices are added together, then pass through these from left to right. Drag to reorder.");

    {
        const auto sum = local (plan.sum);
        const auto disc = juce::Rectangle<float> (26.0f, 26.0f).withCentre (sum.toFloat().getCentre());
        drawRaised (g, disc, 13.0f, p, Elevation::small, p.surface);
        g.setColour (p.ink2);
        g.setFont (uiFont (14.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xce\xa3")), disc, juce::Justification::centred, false);
    }

    drawSectionHeading (g, p, local (plan.modulationHeading), "Modulation",
                        "Drag a coloured handle onto any knob. The arc shows the range; the dot shows the live value.");

    if (effectDrag.active && effectDrag.to != effectDrag.from)
    {
        const auto target = effectPlace (effectDrag.to);
        const float x = effectDrag.to > effectDrag.from ? (float) target.getRight() + (float) L::gap * 0.5f
                                                        : (float) target.getX() - (float) L::gap * 0.5f;
        g.setColour (p.accentInk);
        g.fillRoundedRectangle (x - 2.0f, (float) target.getY() - 4.0f, 4.0f, (float) target.getHeight() + 8.0f, 2.0f);
    }
}

juce::Rectangle<int> PatchView::effectsRow() const
{
    return local (plan.fx[0]).getUnion (local (plan.fx[L::numFx - 1]));
}

void PatchView::resized()
{
    overlay.setBounds (getLocalBounds());

    addButton.setBounds (local (plan.addModuleButton));
    listenRegion.setBounds (local (plan.listenPill));
    perVoiceRegion.setBounds (local (plan.perVoiceZone));
    effectsRegion.setBounds (local (plan.effectsHeading));

    sourcesCard.setBounds (local (plan.sources));

    const auto filterRect = local (plan.filter), filterEnvRect = local (plan.filterEnv), ampEnvRect = local (plan.ampEnv);
    filterCard.setBounds (filterRect);
    filterSlot.setBounds (filterRect);
    filterEnvCard.setBounds (filterEnvRect);
    filterEnvSlot.setBounds (filterEnvRect);
    ampCard.setBounds (local (plan.amp));
    ampEnvCard.setBounds (ampEnvRect);
    ampEnvSlot.setBounds (ampEnvRect);

    // The effects row: present effects take the places from the left, the dashed slots of the others follow.
    const auto slots = effectSlots();
    for (int i = 0; i < slots.numPresent; ++i)
    {
        effectCard (slots.present[(size_t) i]).setPosition (i + 1, slots.numPresent);

        if (! (effectDrag.active && effectDrag.kind == slots.present[(size_t) i])) // a card being dragged stays under the pointer
            effectCard (slots.present[(size_t) i]).setBounds (effectPlace (i));
    }
    for (int i = 0; i < slots.numAbsent; ++i)
        effectSlot (slots.absent[(size_t) i]).setBounds (effectPlace (slots.numPresent + i));

    outputCard.setBounds (local (plan.out));

    lfo1Card.setBounds (local (plan.lfo1));
    lfo1Slot.setBounds (local (plan.lfo1));
    lfo2Card.setBounds (local (plan.lfo2));
    lfo2Slot.setBounds (local (plan.lfo2));
    playingCard.setBounds (local (plan.playing));
    connectionsPanel.setBounds (local (plan.connections));
}

} // namespace scratchpad::ui
