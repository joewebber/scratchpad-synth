#include "ui/PatchView.h"

#include "plugin/Parameters.h"

#include <cmath>

namespace scratchpad::ui
{

using plugin::Module;

namespace
{
    constexpr int panelPad = 14;
    constexpr int cardGap = 6;
    constexpr int chevronWidth = 14;
    constexpr int wellPad = 10;
    constexpr int chipHeight = 28;
    constexpr int voicingHeight = 86;
    constexpr int panelGap = 10;
    constexpr int titleRowTop = 12, titleRowHeight = 24;
    constexpr int wellTop = 44;
    constexpr int perVoiceCaptionHeight = 14;

    constexpr int sourcesWidth = OscillatorCard::width;
    constexpr int filterWidth = FilterCard::width;
    constexpr int ampWidth = AmpCard::width;
    constexpr int wellWidth = wellPad + sourcesWidth + chevronWidth + filterWidth + chevronWidth + ampWidth + wellPad;
    constexpr int rowWidth = wellWidth + chevronWidth + OutputCard::width;

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
      voiceMode (p.apvts, params::voicingMode, "Voice mode", p.undoManager, &Palette::perf),
      voices (p.apvts, params::voicingVoices, "Voices", p.undoManager, "Voices"),
      glide (p.apvts, params::voicingGlide, "Glide", p.undoManager, "Glide time"),
      bendRange (p.apvts, params::voicingBendRange, "Bend", p.undoManager, "Pitch bend range"),
      voiceModeWatcher (*p.apvts.getParameter (params::voicingMode),
                        [this] (float v)
                        {
                            voiceModeIndex = (int) std::lround (v);
                            voices.setEnabled (voiceModeIndex == 0); // mono and legato are one voice
                            repaint (voicingText);
                        })
{
    setTitle ("Signal chain");
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);

    addButton.setTitle ("Add module");
    addButton.setPaintingIsUnclipped (true);
    addButton.getProperties().set (buttonProps::radius, 13.0);
    addButton.onClick = [this] { showAddMenu(); };

    const struct { SlotButton* slot; Module which; } slots[] = {
        { &osc2Slot, Module::osc2 }, { &osc3Slot, Module::osc3 }, { &noiseSlot, Module::noise },
        { &filterSlot, Module::filter }, { &filterEnvSlot, Module::filterEnv }, { &ampEnvSlot, Module::ampEnv }
    };
    for (const auto& s : slots)
    {
        const auto which = s.which;
        s.slot->onClick = [this, which] { processor.addModule (which); };
        addChildComponent (*s.slot);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &osc1Card, &osc2Card, &osc3Card, &noiseCard, &filterCard,
                                                              &filterEnvCard, &ampCard, &ampEnvCard, &outputCard })
        addChildComponent (c);

    osc1Card.setVisible (true);
    ampCard.setVisible (true);
    outputCard.setVisible (true);

    addAndMakeVisible (addButton);
    addAndMakeVisible (voiceMode);
    for (auto* k : { &voices, &glide, &bendRange })
    {
        k->setFamilyColour (paletteOf (*this).perfInk);
        addAndMakeVisible (k);
    }

    for (size_t i = 0; i < plugin::allModules.size(); ++i)
    {
        const auto which = plugin::allModules[i];
        const auto* id = which == Module::osc2 ? params::osc[1].enabled
                       : which == Module::osc3 ? params::osc[2].enabled
                       : which == Module::noise ? params::noiseEnabled
                       : which == Module::filter ? params::filterEnabled
                       : which == Module::ampEnv ? params::ampEnv.enabled
                                                  : params::filterEnv.enabled;
        presenceWatchers[i] = std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (id), [this] (float) { refreshModules(); });
    }

    voiceModeWatcher.sendInitialUpdate();
    refreshModules();
}

PatchView::~PatchView() = default;

juce::String PatchView::listenHint() const
{
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
    for (size_t i = 0; i < plugin::allModules.size(); ++i)
        if (! present (plugin::allModules[i]))
            menu.addItem ((int) i + 1, plugin::ScratchpadProcessor::moduleName (plugin::allModules[i]));

    juce::Component::SafePointer<PatchView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton).withMinimumWidth (addButton.getWidth()),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result >= 1 && result <= (int) plugin::allModules.size())
                                safe->processor.addModule (plugin::allModules[(size_t) result - 1]);
                        });
}

void PatchView::refreshModules()
{
    const bool hasOsc2 = present (Module::osc2), hasOsc3 = present (Module::osc3), hasNoise = present (Module::noise);
    const bool hasFilter = present (Module::filter), hasFilterEnv = present (Module::filterEnv),
               hasAmpEnv = present (Module::ampEnv);

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

    addButton.setEnabled (! (hasOsc2 && hasOsc3 && hasNoise && hasFilter && hasFilterEnv && hasAmpEnv));

    resized();

    // Panels paint their shadows outside their bounds; clear what a removed card left behind.
    if (auto* parent = getParentComponent())
        parent->repaint();
    repaint();

    // The button that was just clicked may have been hidden; keep the computer keyboard playing.
    if (getCurrentlyFocusedComponent() == nullptr)
        if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
            editor->grabKeyboardFocus();
}

//==============================================================================
void PatchView::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    drawRaised (g, chainPanel.toFloat(), 18.0f, p, Elevation::raised, p.surface);
    drawRaised (g, voicingPanel.toFloat(), 18.0f, p, Elevation::raised, p.surface);

    // Title row: "SIGNAL CHAIN" and the one-line "listen for" prompt.
    g.setFont (titleFont());
    g.setColour (p.ink);
    g.drawText ("SIGNAL CHAIN", juce::Rectangle<float> ((float) titleRow.getX(), (float) titleRow.getY(), chainTitleRight - (float) titleRow.getX(), (float) titleRow.getHeight()),
                juce::Justification::centredLeft, false);

    if (listenPill.getWidth() > 60)
    {
        const auto pill = listenPill.toFloat();
        g.setColour (p.accentSoft);
        g.fillRoundedRectangle (pill, 12.0f);

        auto text = pill.reduced (10.0f, 0.0f);
        auto captionFont = uiFont (11.0f);
        captionFont.setExtraKerningFactor (0.14f);
        drawCaption (g, "Listen for", text.removeFromLeft (textWidth (captionFont, "LISTEN FOR") + 2.0f), p.ink2);
        text.removeFromLeft (8.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (12.0f));
        g.drawText (listenHint(), text, juce::Justification::centredLeft, true);
    }

    // The well the per-voice modules sit in.
    drawInset (g, well.toFloat(), 14.0f, p, p.well);
    {
        auto row = juce::Rectangle<float> ((float) well.getX() + (float) wellPad, (float) well.getY() + 8.0f,
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

    // Voicing panel.
    g.setFont (titleFont());
    g.setColour (p.ink);
    g.drawText ("VOICING", juce::Rectangle<int> (voicingPanel.getX() + panelPad, voicingPanel.getY(),
                                                  voicingTitleRight - voicingPanel.getX() - panelPad, voicingPanel.getHeight()),
                juce::Justification::centredLeft, false);

    const char* text = voiceModeIndex == 0
        ? "Poly: every key you hold gets its own voice, up to the Voices limit. Glide slides each voice from the last note it played."
        : voiceModeIndex == 1
            ? "Mono: one voice, newest key wins. Every key restarts the envelopes, and the pitch glides from the note before."
            : "Legato: one voice. Play a key while holding another and the pitch glides without restarting the envelopes.";
    const auto font = uiFont (12.0f);
    g.setFont (font);
    g.setColour (p.mute);
    drawLines (g, wrapLines (font, text, (float) voicingText.getWidth()), voicingText.getPosition().toFloat(),
               (float) voicingText.getWidth(), 16.0f);
}

void PatchView::resized()
{
    const int chainHeight = getHeight() - voicingHeight - panelGap;
    chainPanel = { 0, 0, getWidth(), chainHeight };
    voicingPanel = { 0, chainHeight + panelGap, getWidth(), voicingHeight };

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
    well = { x0, wellTop, wellWidth, chainHeight - wellTop - 12 };
    const int cardsTop = well.getY() + 8 + perVoiceCaptionHeight + 6;

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

    outputCard.setBounds (well.getRight() + chevronWidth, cardsTop, OutputCard::width, OutputCard::height);

    const int chevronY = cardsTop + OscillatorCard::height / 2 - 10;
    chevrons[0] = { sourcesX + sourcesWidth, chevronY, chevronWidth, 20 };
    chevrons[1] = { filterX + filterWidth, chevronY, chevronWidth, 20 };
    chevrons[2] = { well.getRight(), chevronY, chevronWidth, 20 };

    // Voicing panel: title, mode, knobs, then the sentence about the chosen mode.
    voicingTitleRight = voicingPanel.getX() + panelPad + (int) std::ceil (textWidth (font, "VOICING"));
    int x = voicingTitleRight + 18;
    voiceMode.setBounds (x, voicingPanel.getCentreY() - 13, 200, 26);
    x += 200 + 28;

    for (auto* k : { &voices, &glide, &bendRange })
    {
        k->setBounds (x, voicingPanel.getCentreY() - Knob::knobHeight / 2, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + 16;
    }

    x += 16;
    voicingText = { x, voicingPanel.getY() + 16, voicingPanel.getRight() - panelPad - x, voicingPanel.getHeight() - 32 };
}

} // namespace scratchpad::ui
