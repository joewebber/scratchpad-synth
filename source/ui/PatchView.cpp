#include "ui/PatchView.h"

#include "plugin/Parameters.h"
#include "plugin/Suggestion.h"

#include <cmath>

namespace scratchpad::ui
{

using plugin::Module;

namespace
{
    constexpr int panelPad = 14;
    constexpr int cardGap = 5;
    constexpr int chevronWidth = 14;
    constexpr int wellPad = 10;
    constexpr int chipHeight = 28;
    constexpr int panelGap = 10;
    constexpr int titleRowTop = 10, titleRowHeight = 22;
    constexpr int wellTop = 36;
    constexpr int wellBottomMargin = 8;
    constexpr int perVoiceCaptionTop = 6, perVoiceCaptionHeight = 14;
    constexpr int stripPad = 8, stripSidePad = 12, stripGap = 10;

    constexpr int sourcesWidth = OscillatorCard::width;
    constexpr int filterWidth = FilterCard::width;
    constexpr int ampWidth = AmpCard::width;
    constexpr int wellWidth = wellPad + sourcesWidth + chevronWidth + filterWidth + chevronWidth + ampWidth + wellPad;
    constexpr int rowWidth = wellWidth + chevronWidth + OutputCard::width;

    constexpr int stripHeight = LfoCard::height + 2 * stripPad;

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

    juce::Font titleFont()
    {
        auto font = uiFont (13.0f, Weight::medium);
        font.setExtraKerningFactor (0.22f);
        return font;
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
PatchView::PatchView (plugin::ScratchpadProcessor& p)
    : processor (p),
      osc2Slot ("Add Oscillator 2", "Oscillator 2", SlotButton::Style::chip),
      osc3Slot ("Add Oscillator 3", "Oscillator 3", SlotButton::Style::chip),
      noiseSlot ("Add Noise", "Noise", SlotButton::Style::chip),
      filterSlot ("Add Filter", "Filter", SlotButton::Style::slot, "Cut or boost parts of the sound"),
      filterEnvSlot ("Add Filter Envelope", "Filter Env", SlotButton::Style::slot, "Open and close the filter over each note"),
      ampEnvSlot ("Add Amp Envelope", "Amp Env", SlotButton::Style::slot, "Shape how each note starts and ends"),
      lfo1Slot ("Add LFO 1", "LFO 1", SlotButton::Style::slot, "A slow wobble for vibrato and sweeps"),
      lfo2Slot ("Add LFO 2", "LFO 2", SlotButton::Style::slot, "A second, independent wobble")
{
    setTitle ("Signal chain");
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);

    addButton.setTitle ("Add module");
    addButton.setPaintingIsUnclipped (true);
    addButton.getProperties().set (buttonProps::radius, 13.0);
    addButton.onClick = [this] { showAddMenu(); };

    addButton.getProperties().set ("explainId", "control.addModule");
    for (auto* slot : { &osc2Slot, &osc3Slot, &noiseSlot, &filterSlot, &filterEnvSlot, &ampEnvSlot, &lfo1Slot, &lfo2Slot })
        slot->getProperties().set ("explainId", "control.addModule");

    const struct { SlotButton* slot; Module which; } slots[] = {
        { &osc2Slot, Module::osc2 }, { &osc3Slot, Module::osc3 }, { &noiseSlot, Module::noise },
        { &filterSlot, Module::filter }, { &filterEnvSlot, Module::filterEnv }, { &ampEnvSlot, Module::ampEnv },
        { &lfo1Slot, Module::lfo1 }, { &lfo2Slot, Module::lfo2 }
    };
    for (const auto& s : slots)
    {
        const auto which = s.which;
        s.slot->onClick = [this, which] { processor.addModule (which); };
        addChildComponent (*s.slot);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &osc1Card, &osc2Card, &osc3Card, &noiseCard, &filterCard,
                                                              &filterEnvCard, &ampCard, &ampEnvCard, &voicingCard, &outputCard,
                                                              &lfo1Card, &lfo2Card })
        addChildComponent (c);

    osc1Card.setVisible (true);
    ampCard.setVisible (true);
    voicingCard.setVisible (true);
    outputCard.setVisible (true);

    addAndMakeVisible (addButton);
    addAndMakeVisible (sourcesPanel);
    addAndMakeVisible (connectionsPanel);
    addAndMakeVisible (overlay); // last, so it is on top

    for (size_t i = 0; i < plugin::allModules.size(); ++i)
        presenceWatchers[i] = std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (presenceId (plugin::allModules[i])), [this] (float) { refreshModules(); });

    model.addListener (this);
    refreshModules();
}

PatchView::~PatchView()
{
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
        { "Movement", { Module::lfo1, Module::lfo2 } }
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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton).withMinimumWidth (addButton.getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result >= 1 && result <= (int) plugin::allModules.size())
                                safe->processor.addModule (plugin::allModules[(size_t) result - 1]);
                        });
}

plugin::Presence PatchView::presence() const
{
    plugin::Presence result {};
    for (const auto m : plugin::allModules)
        result[(size_t) m] = present (m);
    return result;
}

void PatchView::refreshModules()
{
    const bool hasOsc2 = present (Module::osc2), hasOsc3 = present (Module::osc3), hasNoise = present (Module::noise);
    const bool hasFilter = present (Module::filter), hasFilterEnv = present (Module::filterEnv),
               hasAmpEnv = present (Module::ampEnv), hasLfo1 = present (Module::lfo1), hasLfo2 = present (Module::lfo2);

    osc2Card.setVisible (hasOsc2);
    osc2Slot.setVisible (! hasOsc2);
    osc3Card.setVisible (hasOsc3);
    osc3Slot.setVisible (! hasOsc3);
    noiseCard.setVisible (hasNoise);
    noiseSlot.setVisible (! hasNoise);
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

    // The palette's advice: the dashed slot of the suggested module, if it has one on screen.
    const auto suggestion = plugin::suggestNext (presence());
    const struct { SlotButton* slot; Module which; } suggestable[] = {
        { &osc2Slot, Module::osc2 }, { &osc3Slot, Module::osc3 }, { &noiseSlot, Module::noise },
        { &filterSlot, Module::filter }, { &filterEnvSlot, Module::filterEnv }, { &ampEnvSlot, Module::ampEnv },
        { &lfo1Slot, Module::lfo1 }, { &lfo2Slot, Module::lfo2 }
    };
    for (const auto& s : suggestable)
        s.slot->setSuggested (suggestion && suggestion->module == s.which, suggestion ? suggestion->reason : "");

    sourcesPanel.refreshPresence();

    // A handle that was armed may belong to a module that has just gone.
    if (model.isConnecting() && ! processor.isModSourcePresent (model.getConnectingSource()))
        model.cancelConnecting();

    bool everything = true;
    for (const auto m : plugin::allModules)
        everything = everything && present (m);
    addButton.setEnabled (! everything);

    resized();

    // Panels paint their shadows outside their bounds; clear what a removed card left behind.
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
void PatchView::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    drawRaised (g, chainPanel.toFloat(), 18.0f, p, Elevation::raised, p.surface);
    drawRaised (g, stripPanel.toFloat(), 18.0f, p, Elevation::raised, p.surface);

    // Title row: "SIGNAL CHAIN" and the one-line prompt.
    g.setFont (titleFont());
    g.setColour (p.ink);
    g.drawText ("SIGNAL CHAIN", juce::Rectangle<float> ((float) titleRow.getX(), (float) titleRow.getY(), chainTitleRight - (float) titleRow.getX(), (float) titleRow.getHeight()),
                juce::Justification::centredLeft, false);

    if (listenPill.getWidth() > 60)
    {
        const bool hasStatus = model.getStatus().isNotEmpty();
        const auto pill = listenPill.toFloat();
        g.setColour (hasStatus ? p.accent.withAlpha (0.9f) : p.accentSoft);
        g.fillRoundedRectangle (pill, 11.0f);

        auto text = pill.reduced (10.0f, 0.0f);

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

    // The well the per-voice modules sit in.
    drawInset (g, well.toFloat(), 14.0f, p, p.well);
    {
        auto row = juce::Rectangle<float> ((float) well.getX() + (float) wellPad, (float) well.getY() + (float) perVoiceCaptionTop,
                                           (float) well.getWidth() - 2.0f * (float) wellPad, (float) perVoiceCaptionHeight);
        auto captionFont = uiFont (11.0f);
        captionFont.setExtraKerningFactor (0.14f);
        const auto captionWidth = textWidth (captionFont, "PER VOICE") + 14.0f;
        drawCaption (g, "Per voice", row.removeFromLeft (captionWidth), p.ink2);
        g.setColour (p.mute);
        g.setFont (uiFont (11.0f));
        g.drawText ("Every note you hold gets its own copy of this section", row, juce::Justification::centredLeft, true);
    }

    for (const auto& c : chevrons)
        drawChevron (g, p, c);
}

void PatchView::resized()
{
    overlay.setBounds (getLocalBounds());

    const int chainHeight = getHeight() - stripHeight - panelGap;
    chainPanel = { 0, 0, getWidth(), chainHeight };
    stripPanel = { 0, chainHeight + panelGap, getWidth(), stripHeight };

    // Title row.
    const auto font = titleFont();
    chainTitleRight = (float) panelPad + textWidth (font, "SIGNAL CHAIN");
    titleRow = { panelPad, titleRowTop, getWidth() - 2 * panelPad, titleRowHeight };

    const int addWidth = 108;
    addButton.setBounds (getWidth() - panelPad - addWidth, titleRowTop - 1, addWidth, titleRowHeight + 2);
    const int pillX = (int) std::ceil (chainTitleRight) + 12;
    listenPill = { pillX, titleRowTop, addButton.getX() - 12 - pillX, titleRowHeight };

    // The row of stages, centred in the panel.
    const int x0 = panelPad + (getWidth() - 2 * panelPad - rowWidth) / 2;
    well = { x0, wellTop, wellWidth, chainHeight - wellTop - wellBottomMargin };
    const int cardsTop = well.getY() + perVoiceCaptionTop + perVoiceCaptionHeight + 4;

    // Sources: the cards that are present, then one row of dashed chips for the ones that are not.
    const int sourcesX = well.getX() + wellPad;
    int y = cardsTop;
    osc1Card.setBounds (sourcesX, y, OscillatorCard::width, OscillatorCard::height);
    y += OscillatorCard::height + cardGap;

    struct Source { juce::Component* card; SlotButton* slot; bool isPresent; };
    const Source sources[] = {
        { &osc2Card, &osc2Slot, present (Module::osc2) },
        { &osc3Card, &osc3Slot, present (Module::osc3) },
        { &noiseCard, &noiseSlot, present (Module::noise) }
    };

    int missing = 0;
    for (const auto& s : sources)
    {
        if (s.isPresent)
        {
            s.card->setBounds (sourcesX, y, OscillatorCard::width, OscillatorCard::height);
            y += OscillatorCard::height + cardGap;
        }
        else
            ++missing;
    }

    if (missing > 0)
    {
        const int chipWidth = (sourcesWidth - (missing - 1) * cardGap) / missing;
        int x = sourcesX;
        for (const auto& s : sources)
            if (! s.isPresent)
            {
                s.slot->setBounds (x, y, chipWidth, chipHeight);
                x += chipWidth + cardGap;
            }
    }

    // Filter column, then amp column; each envelope sits under the stage it shapes.
    const int filterX = sourcesX + sourcesWidth + chevronWidth;
    const int ampX = filterX + filterWidth + chevronWidth;

    const juce::Rectangle<int> filterRect (filterX, cardsTop, FilterCard::width, FilterCard::height);
    filterCard.setBounds (filterRect);
    filterSlot.setBounds (filterRect);

    const juce::Rectangle<int> filterEnvRect (filterX, filterRect.getBottom() + cardGap, EnvelopeCard::width, EnvelopeCard::height);
    filterEnvCard.setBounds (filterEnvRect);
    filterEnvSlot.setBounds (filterEnvRect);

    ampCard.setBounds (ampX, cardsTop, AmpCard::width, AmpCard::height);

    const juce::Rectangle<int> ampEnvRect (ampX, cardsTop + AmpCard::height + cardGap, EnvelopeCard::width, EnvelopeCard::height);
    ampEnvCard.setBounds (ampEnvRect);
    ampEnvSlot.setBounds (ampEnvRect);

    voicingCard.setBounds (ampX, ampEnvRect.getBottom() + cardGap, VoicingCard::width, VoicingCard::height);

    outputCard.setBounds (well.getRight() + chevronWidth, cardsTop, OutputCard::width, OutputCard::height);

    const int chevronY = cardsTop + OscillatorCard::height / 2 - 10;
    chevrons[0] = { sourcesX + sourcesWidth, chevronY, chevronWidth, 20 };
    chevrons[1] = { filterX + filterWidth, chevronY, chevronWidth, 20 };
    chevrons[2] = { well.getRight(), chevronY, chevronWidth, 20 };

    // Modulation strip: LFO 1, LFO 2, the other sources, then the eight slots.
    int x = stripPanel.getX() + stripSidePad;
    const int stripY = stripPanel.getY() + stripPad;

    const juce::Rectangle<int> lfo1Rect (x, stripY, LfoCard::width, LfoCard::height);
    lfo1Card.setBounds (lfo1Rect);
    lfo1Slot.setBounds (lfo1Rect);
    x += LfoCard::width + stripGap;

    const juce::Rectangle<int> lfo2Rect (x, stripY, LfoCard::width, LfoCard::height);
    lfo2Card.setBounds (lfo2Rect);
    lfo2Slot.setBounds (lfo2Rect);
    x += LfoCard::width + stripGap;

    sourcesPanel.setBounds (x, stripY, SourcesPanel::width, SourcesPanel::height);
    x += SourcesPanel::width + stripGap;

    connectionsPanel.setBounds (x, stripY, stripPanel.getRight() - stripSidePad - x, ConnectionsPanel::height);
}

} // namespace scratchpad::ui
