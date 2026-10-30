#include "ui/ModuleCards.h"

#include "plugin/Parameters.h"
#include "ui/ExplainPanel.h"
#include "ui/WaveGlyphs.h"

#include <cmath>
#include <utility>

namespace scratchpad::ui
{

using dsp::Wave;

namespace
{
    // Glyphs from the prototype: 18 x 10 for the wave selector, 12 x 10 for module headers.
    const char* selectorGlyph (Wave w)
    {
        switch (w)
        {
            case Wave::sine:     return "M1 5c2-5.5 5-5.5 8 0s6 5.5 8 0";
            case Wave::triangle: return "M1 5l4-4 8 8 4-4";
            case Wave::saw:      return "M1 1l8 8V1l8 8";
            case Wave::pulse:    return "M1 9V1h8v8h8V1";
            case Wave::wavetableA:
            case Wave::wavetableB:
            case Wave::wavetableC: return "M1 5c2-5.5 5-5.5 8 0s6 5.5 8 0";
        }
        return "";
    }

    const char* headerGlyph (Wave w)
    {
        switch (w)
        {
            case Wave::sine:     return "M0.5 5c1.4-5 3.6-5 5.5 0s4 5 5.5 0";
            case Wave::triangle: return "M0.5 5l2.75-4 5.5 8 2.75-4";
            case Wave::saw:      return "M0.5 1.5L6 8.5V1.5L11.5 8.5";
            case Wave::pulse:    return "M0.5 9V1h5.5v8h5.5V1";
            case Wave::wavetableA:
            case Wave::wavetableB:
            case Wave::wavetableC: return "M0.5 5c1.4-5 3.6-5 5.5 0s4 5 5.5 0";
        }
        return "";
    }

    constexpr const char* noiseGlyph = "M0.5 5l1.5-3 1.5 6 1.5-5 1.5 4 1.5-3 1.5 4 1.5-3";
    constexpr const char* filterGlyph = "M0.5 3h5.5c1.5 0 2-1 2.5 0s1.5 6 3 6.5";
    constexpr const char* gateGlyph = "M0.5 9h2.5V1.5h6V9h2.5";
    constexpr const char* envelopeGlyph = "M0.5 9L3.5 1.5l3 3.5h3L11.5 9";
    constexpr const char* outputGlyph = "M1 5h9 M7.5 2l3 3-3 3";

    std::vector<const char*> waveSelectorGlyphs()
    {
        std::vector<const char*> glyphs;
        for (int i = 0; i < dsp::numWaves; ++i)
            glyphs.push_back (selectorGlyph ((Wave) i));
        return glyphs;
    }

    juce::String oscName (int index) { return "Oscillator " + juce::String (index + 1); }

    // The knobs of a card sit on a fixed 52 px pitch.
    constexpr int knobPitch = Knob::knobWidth;
}

//==============================================================================
ModuleCard::ModuleCard()
{
    setPaintingIsUnclipped (true);
    setFocusContainerType (FocusContainerType::focusContainer); // so a screen reader nests the controls under their card
}

void ModuleCard::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::small, p.surface);
}

std::unique_ptr<juce::AccessibilityHandler> ModuleCard::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group,
                                                     [this]
                                                     {
                                                         const auto* entry = content::Content::get().module (getProperties()["explainId"].toString());
                                                         return entry != nullptr ? entry->what : juce::String();
                                                     });
}

void ModuleCard::setFocusOrder (const std::vector<juce::Component*>& controls, juce::Component* entry)
{
    int order = 1;

    for (auto* c : std::initializer_list<juce::Component*> { bypassButton.get(), soloButton.get(), explainButton.get(), removeButton.get() })
        if (c != nullptr)
            c->setExplicitFocusOrder (order++);

    entryControl = entry != nullptr ? entry : controls.empty() ? nullptr : controls.front();

    for (auto* c : controls)
        c->setExplicitFocusOrder (order++);
}

void ModuleCard::focusFirstControl()
{
    if (entryControl != nullptr)
        entryControl->grabKeyboardFocus();
}

void ModuleCard::makeRemovable (const juce::String& accessibleName, std::function<void()> onRemove)
{
    removeButton = std::make_unique<IconButton> (accessibleName, "M5.5 5.5l5 5 M10.5 5.5l-5 5", 16.0f, 1.3f);
    removeButton->setDescription ("Takes this module out of the patch and puts its settings back to their defaults. You can undo it.");
    removeButton->getProperties().set ("explainId", "control.remove");
    removeButton->onClick = std::move (onRemove);
    addAndMakeVisible (*removeButton);
}

void ModuleCard::placeRemoveButton (juce::Rectangle<int> row)
{
    constexpr int w = 22, h = 20, gap = 3;

    const auto place = [&] (juce::Component* c)
    {
        if (c != nullptr)
        {
            c->setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, h));
            row.removeFromRight (gap);
        }
    };

    place (removeButton.get());
    place (explainButton.get());

    if (! bypassSoloElsewhere)
    {
        place (soloButton.get());
        place (bypassButton.get());
    }
}

void ModuleCard::placeBypassSolo (juce::Point<int> topLeft)
{
    if (bypassButton != nullptr)
        bypassButton->setBounds (topLeft.x, topLeft.y, 22, 20);

    if (soloButton != nullptr)
        soloButton->setBounds (topLeft.x + 25, topLeft.y, 22, 20);
}

void ModuleCard::setExplainKey (const juce::String& moduleKey)
{
    getProperties().set ("explainId", moduleKey);
}

void ModuleCard::makeControllable (plugin::ScratchpadProcessor& p, const juce::String& key, bool soloable, bool bypassable)
{
    controlled = &p;
    controlKey = key;
    isSoloable = soloable;
    setExplainKey (key);

    explainButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::explain, "Explain " + getTitle());
    explainButton->setDescription ("Shows what the " + getTitle() + " is and does in the Explain panel, and keeps it there.");
    explainButton->onClick = [this] { ExplainPanel::pinFrom (*this, controlKey); };
    explainButton->getProperties().set ("explainId", key);
    addAndMakeVisible (*explainButton);

    if (bypassable)
    {
        bypassButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::bypass, getTitle() + " bypass");
        bypassButton->onClick = [this] { controlled->setBypassed (controlKey, ! controlled->isBypassed (controlKey)); };
        addAndMakeVisible (*bypassButton);
        stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (key + ".bypass"), [this] (float) { refreshState(); }));
    }

    if (soloable)
    {
        soloButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::solo, getTitle() + " solo");
        soloButton->onClick = [this] { controlled->setSoloed (controlKey, ! controlled->isSoloed (controlKey)); };
        addAndMakeVisible (*soloButton);

        // Any source's solo changes whether this one is heard.
        for (int i = 0; i < params::numSoloable; ++i)
            stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
                *p.apvts.getParameter (juce::String (params::bypassKeys[(size_t) i]) + ".solo"), [this] (float) { refreshState(); }));
    }

    refreshState();
}

void ModuleCard::refreshState()
{
    if (controlled == nullptr)
        return;

    const bool bypassed = bypassButton != nullptr && controlled->isBypassed (controlKey);
    const bool soloed = isSoloable && controlled->isSoloed (controlKey);

    bool anySolo = false;
    for (int i = 0; i < params::numSoloable; ++i)
        anySolo = anySolo || controlled->isSoloed (params::bypassKeys[(size_t) i]);

    if (bypassButton != nullptr)
        bypassButton->setOn (bypassed);

    if (soloButton != nullptr)
        soloButton->setOn (soloed);

    // A bypassed module, or a source another one's solo has muted, is shown faded.
    setAlpha (bypassed || (isSoloable && anySolo && ! soloed) ? 0.5f : 1.0f);
}

void ModuleCard::drawHeader (juce::Graphics& g, const Palette& p, juce::Rectangle<float> row,
                             juce::Colour family, const char* glyph, const juce::String& title)
{
    const auto dot = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ row.getX() + 10.0f, row.getCentreY() });
    g.setColour (family);
    g.fillEllipse (dot);

    auto path = juce::Drawable::parseSVGPath (glyph);
    path.applyTransform (juce::AffineTransform::translation (dot.getX() + 4.0f, dot.getY() + 5.0f));
    g.setColour (p.onAccent);
    g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    g.setColour (p.ink);
    g.setFont (uiFont (13.0f, Weight::medium));
    g.drawText (title, row.withTrimmedLeft (27.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
MoreButton::MoreButton (const juce::String& oscillatorName)
    : juce::Button (oscillatorName + " more options"), name (oscillatorName)
{
    setPaintingIsUnclipped (true);
    setClickingTogglesState (false);
    setToggleable (true);
    setTitle (oscillatorName + " more options");
    getProperties().set ("explainId", "control.oscMore");
    refreshTitle();
}

void MoreButton::refreshTitle()
{
    // The title stays put: "pressed" (open) is the toggle state, and what the dot means is in the description.
    setToggleState (open, juce::dontSendNotification);
    setDescription ("Shows or hides the extras for " + name + ": unison, anti-aliasing and more."
                    + (marker == Marker::warning ? " Anti-aliasing is off." : marker == Marker::inUse ? " Some extras are in use." : juce::String()));
}

void MoreButton::setOpen (bool shouldBeOpen)
{
    if (open != shouldBeOpen)
    {
        open = shouldBeOpen;
        refreshTitle();
        repaint();
    }
}

void MoreButton::setMarker (Marker newMarker)
{
    if (marker != newMarker)
    {
        marker = newMarker;
        refreshTitle();
        repaint();
    }
}

void MoreButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    auto fill = open ? p.well : p.surface;
    if (down)
        fill = fill.interpolatedWith (p.well, 0.5f);
    else if (highlighted)
        fill = fill.interpolatedWith (p.ink, 0.06f);

    if (open)
        drawInset (g, bounds, 7.0f, p, fill);
    else
        drawRaised (g, bounds, 7.0f, p, Elevation::small, fill);

    const auto centre = bounds.getCentre();
    const float dir = open ? -1.0f : 1.0f;
    juce::Path chevron;
    chevron.startNewSubPath (centre.x - 3.5f, centre.y - 1.5f * dir);
    chevron.lineTo (centre.x, centre.y + 2.0f * dir);
    chevron.lineTo (centre.x + 3.5f, centre.y - 1.5f * dir);
    g.setColour (p.ink2);
    g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (marker != Marker::none)
    {
        const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ bounds.getRight() - 2.0f, bounds.getY() + 2.0f });
        g.setColour (p.surface);
        g.fillEllipse (dot.expanded (1.5f));
        g.setColour (marker == Marker::warning ? p.accentInk : p.srcInk);
        g.fillEllipse (dot);
    }

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds, 7.0f, p.ink);
}

//==============================================================================
OscillatorCard::OscillatorCard (plugin::ScratchpadProcessor& p, ModulationModel& model, int oscillatorIndex)
    : index (oscillatorIndex),
      processor (p),
      waveSelector (p.apvts, params::osc[(size_t) oscillatorIndex].wave, oscName (oscillatorIndex) + " wave", p.undoManager, &Palette::src),
      phaseReset (p.apvts, params::osc[(size_t) oscillatorIndex].phaseReset, "Phase reset",
                  oscName (oscillatorIndex) + " phase reset", p.undoManager, &Palette::srcInk),
      octave (p.apvts, params::osc[(size_t) oscillatorIndex].octave, "Octave", p.undoManager, oscName (oscillatorIndex) + " octave"),
      semitone (p.apvts, params::osc[(size_t) oscillatorIndex].semitone, "Semi", p.undoManager, oscName (oscillatorIndex) + " semitone"),
      fine (p.apvts, params::osc[(size_t) oscillatorIndex].fine, "Fine", p.undoManager, oscName (oscillatorIndex) + " fine tune"),
      level (p.apvts, params::osc[(size_t) oscillatorIndex].level, "Level", p.undoManager, oscName (oscillatorIndex) + " level"),
      pulseWidth (p.apvts, params::osc[(size_t) oscillatorIndex].pulseWidth, "Pulse", p.undoManager, oscName (oscillatorIndex) + " pulse width"),
      moreButton (oscName (oscillatorIndex)),
      waveWatcher (*p.apvts.getParameter (params::osc[(size_t) oscillatorIndex].wave),
                   [this] (float v) { setWave ((int) std::lround (v)); })
{
    setTitle (oscName (index));

    waveSelector.setGlyphs (waveSelectorGlyphs());
    if (index == 0)
        waveSelector.onPick = [&p] (int w) { p.changeWave (w); };

    if (index > 0)
    {
        const auto which = index == 1 ? plugin::Module::osc2 : plugin::Module::osc3;
        makeRemovable ("Remove " + oscName (index), [&p, which] { p.removeModule (which); });
    }

    makeControllable (p, "osc" + juce::String (index + 1), true);
    bypassSoloElsewhere = true; // the header is too narrow: B and S sit under the wave picture

    // Fine tune is the pitch knob: a drop there moves this oscillator's pitch, or all of them.
    fine.setModulation (model, index == 0 ? std::vector<engine::ModDest> { engine::ModDest::pitchAll, engine::ModDest::pitch1 }
                                          : std::vector<engine::ModDest> { engine::pitchDest ((size_t) index), engine::ModDest::pitchAll });
    level.setModulation (model, { engine::levelDest ((size_t) index) });
    pulseWidth.setModulation (model, { engine::pulseWidthDest ((size_t) index) });

    moreButton.onClick = [this] { if (onMoreClicked) onMoreClicked(); };

    for (auto* c : std::initializer_list<juce::Component*> { &waveSelector, &phaseReset, &octave, &semitone, &fine, &level, &pulseWidth, &moreButton, &waveRegion })
        addAndMakeVisible (c);

    waveRegion.setTitle (oscName (index) + " waveform");
    waveRegion.setDescriber ([this]
                             {
                                 return params::waveNames()[wave] + " wave" + (aliasingOff ? ". Anti-aliasing is off, so expect harsh extra tones." : juce::String());
                             });
    setFocusOrder ({ &waveSelector, &waveRegion, &phaseReset, &moreButton, &octave, &semitone, &fine, &level, &pulseWidth });

    // The chevron's dot and the wave well's warning follow the extras' parameters, however they change.
    const auto& ids = params::osc[(size_t) index];
    std::vector<const char*> watched { ids.antiAlias, ids.unison };
    if (ids.sync != nullptr)
        watched.push_back (ids.sync);
    if (index == 0)
    {
        watched.push_back (params::ringLevel);
        watched.push_back (params::fmAmount);
    }
    for (auto* id : watched)
        extrasWatchers.push_back (std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (id), [this] (float) { refreshExtras(); }));

    applyColours();
    waveWatcher.sendInitialUpdate();
    refreshExtras();
}

float OscillatorCard::raw (const char* id) const
{
    // The parameter's own value: a listener runs before the cached atomic is updated.
    auto* param = processor.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void OscillatorCard::refreshExtras()
{
    const auto& ids = params::osc[(size_t) index];

    aliasingOff = raw (ids.antiAlias) < 0.5f;

    bool inUse = raw (ids.unison) > 1.5f;
    if (ids.sync != nullptr)
        inUse = inUse || raw (ids.sync) > 0.5f;
    if (index == 0)
        inUse = inUse || raw (params::ringLevel) > params::ringLevelDefaultDb + 0.5f || raw (params::fmAmount) > 0.5f;

    moreButton.setMarker (aliasingOff ? MoreButton::Marker::warning : inUse ? MoreButton::Marker::inUse : MoreButton::Marker::none);
    repaint (waveWell.expanded (4));
}

void OscillatorCard::applyColours()
{
    const auto ink = paletteOf (*this).srcInk;
    for (auto* k : { &octave, &semitone, &fine, &level, &pulseWidth })
        k->setFamilyColour (ink);
}

void OscillatorCard::lookAndFeelChanged()
{
    applyColours();
}

void OscillatorCard::setWave (int newWave)
{
    wave = juce::jlimit (0, dsp::numWaves - 1, newWave);
    pulseWidth.setVisible (wave == (int) Wave::pulse);
    repaint();
}

void OscillatorCard::resized()
{
    constexpr int top = 4, midX = 70, midWidth = 104, knobsX = 182, resetWidth = 88, moreWidth = 20;

    waveWell = { pad, top, 56, 46 };
    waveRegion.setBounds (waveWell);
    placeBypassSolo ({ pad + 4, top + 50 });
    headerRow = { midX, top, midWidth, 20 };
    placeRemoveButton (headerRow);
    waveSelector.setBounds (midX, top + 25, midWidth, 24);
    phaseReset.setBounds (midX, top + 54, resetWidth, 20);
    moreButton.setBounds (midX + resetWidth + 1, top + 54, moreWidth, 20);

    int x = knobsX;
    for (auto* k : { &octave, &semitone, &fine, &level, &pulseWidth })
    {
        k->setBounds (x, top, Knob::knobWidth, Knob::knobHeight);
        x += knobPitch;
    }
}

void OscillatorCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (index > 0 ? 26.0f : 25.0f), p.src, headerGlyph ((Wave) wave),
                "Osc " + juce::String (index + 1));

    drawInset (g, waveWell.toFloat(), 10.0f, p, p.well);
    const auto drawArea = waveWell.toFloat().reduced (6.0f, 0.0f);
    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (p.track);
    g.drawDashedLine ({ drawArea.getX(), drawArea.getCentreY(), drawArea.getRight(), drawArea.getCentreY() }, dashes, 2, 1.0f);
    g.setColour (p.srcInk);
    g.strokePath (wavePath ((Wave) wave, drawArea, 1.5f, 0.6f),
                  juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    // Anti-aliasing off is a state to notice, not an alarm: an accent outline and a small tag on the picture.
    if (aliasingOff)
    {
        g.setColour (p.accentInk);
        g.drawRoundedRectangle (waveWell.toFloat().reduced (0.75f), 10.0f, 1.5f);

        const auto font = uiFont (11.0f, Weight::medium);
        const float tagWidth = textWidth (font, "AA off") + 10.0f;
        const auto tag = juce::Rectangle<float> (tagWidth, 14.0f).withCentre ({ (float) waveWell.getCentreX(), (float) waveWell.getBottom() - 11.0f });
        g.setColour (p.accent);
        g.fillRoundedRectangle (tag, 7.0f);
        g.setColour (p.onAccent);
        g.setFont (font);
        g.drawText ("AA off", tag, juce::Justification::centred, false);
    }
}

//==============================================================================
OscillatorExtras::OscillatorExtras (plugin::ScratchpadProcessor& p, int oscillatorIndex)
    : processor (p),
      index (oscillatorIndex),
      unison (p.apvts, params::osc[(size_t) oscillatorIndex].unison, "Unison", p.undoManager, oscName (oscillatorIndex) + " unison voices"),
      detune (p.apvts, params::osc[(size_t) oscillatorIndex].unisonDetune, "Detune", p.undoManager, oscName (oscillatorIndex) + " unison detune"),
      spread (p.apvts, params::osc[(size_t) oscillatorIndex].unisonSpread, "Spread", p.undoManager, oscName (oscillatorIndex) + " unison spread"),
      antiAlias (p.apvts, params::osc[(size_t) oscillatorIndex].antiAlias, "Anti-aliasing",
                 oscName (oscillatorIndex) + " anti-aliasing", p.undoManager, &Palette::srcInk)
{
    setTitle (oscName (index) + " extras");
    setExplainKey ("osc" + juce::String (index + 1));

    for (auto* c : std::initializer_list<juce::Component*> { &unison, &detune, &spread, &antiAlias })
        addAndMakeVisible (c);

    addAndMakeVisible (statusRegion);

    watch (params::osc[(size_t) index].antiAlias);
    watch (params::osc[(size_t) index].unison);

    if (index > 0)
    {
        const auto& ids = params::osc[(size_t) index];
        sync = std::make_unique<ToggleChip> (p.apvts, ids.sync, "Sync to Osc 1", oscName (index) + " hard sync", p.undoManager, &Palette::srcInk);
        addAndMakeVisible (*sync);
        watch (ids.sync);
        setFocusOrder ({ &unison, &detune, &spread, &antiAlias, sync.get(), &statusRegion });
    }
    else
    {
        // Oscillator 1 acts on the others, so ring and FM live here, with the oscillators they need as watched presence.
        ring = std::make_unique<Knob> (p.apvts, params::ringLevel, "Ring", p.undoManager, "Ring modulation level");
        fm = std::make_unique<Knob> (p.apvts, params::fmAmount, "FM", p.undoManager, "FM amount");
        fmTarget = std::make_unique<ChoiceSegments> (p.apvts, params::fmTarget, "FM target", p.undoManager, &Palette::src);

        for (auto* c : std::initializer_list<juce::Component*> { ring.get(), fm.get(), fmTarget.get() })
            addAndMakeVisible (c);

        ring->setDisabledNote ("Not in use: it needs Oscillator 2 in the patch.");
        fm->setDisabledNote ("Not in use: its target oscillator is not in the patch.");
        setFocusOrder ({ &unison, &detune, &spread, ring.get(), fm.get(), fmTarget.get(), &antiAlias, &statusRegion });

        watch (params::ringLevel);
        watch (params::fmAmount);
        watch (params::fmTarget);
        watch (params::osc[1].enabled);
        watch (params::osc[2].enabled);
    }

    applyColours();
    refreshState();
}

float OscillatorExtras::raw (const char* id) const
{
    auto* param = processor.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

void OscillatorExtras::watch (const char* id)
{
    watchers.push_back (std::make_unique<juce::ParameterAttachment> (*processor.apvts.getParameter (id), [this] (float) { refreshState(); }));
}

void OscillatorExtras::applyColours()
{
    const auto ink = paletteOf (*this).srcInk;
    for (auto* k : { &unison, &detune, &spread })
        k->setFamilyColour (ink);

    if (ring != nullptr)
    {
        ring->setFamilyColour (ink);
        fm->setFamilyColour (ink);
    }
}

void OscillatorExtras::lookAndFeelChanged()
{
    applyColours();
}

void OscillatorExtras::refreshState()
{
    const auto& ids = params::osc[(size_t) index];
    const bool aliasingOff = raw (ids.antiAlias) < 0.5f;

    if (index == 0)
    {
        // Ring and FM work on other oscillators: with none to work on, they dim and the line below says why.
        const bool hasOsc2 = processor.isModulePresent (plugin::Module::osc2), hasOsc3 = processor.isModulePresent (plugin::Module::osc3);
        const int target = (int) std::lround (raw (params::fmTarget));
        const bool targetPresent = target == 0 ? hasOsc2 : target == 1 ? hasOsc3 : (hasOsc2 || hasOsc3);

        ring->setEnabled (hasOsc2);
        fm->setEnabled (targetPresent);

        if (aliasingOff)
            status = "Anti-aliasing is off: expect harsh, glassy extra tones, most of all on high notes.";
        else if (! hasOsc2 && ! hasOsc3)
            status = "Ring modulation and FM act on Oscillator 2 or 3: add one to hear them.";
        else if (! hasOsc2 && raw (params::ringLevel) > params::ringLevelDefaultDb + 0.5f)
            status = "Ring modulation needs Oscillator 2 in the patch.";
        else if (! targetPresent)
            status = "FM has no target in the patch: pick another, or add that oscillator.";
        else
            status = "Ring and FM mix Oscillator 1 into the others. Unison thickens it.";
    }
    else if (aliasingOff)
        status = "Anti-aliasing is off: expect harsh, glassy extra tones, most of all on high notes.";
    else if (raw (ids.sync) > 0.5f)
        status = "Sync restarts this oscillator with Oscillator 1: tight, tearing tones as you detune it.";
    else if (raw (ids.unison) > 1.5f)
        status = juce::String ((int) std::lround (raw (ids.unison))) + " copies, detuned and spread across the stereo field.";
    else
        status = "Unison stacks detuned copies for a thicker, wider sound.";

    statusIsWarning = aliasingOff;
    statusRegion.setText (status);
    repaint();
}

void OscillatorExtras::resized()
{
    constexpr int top = 6;

    int x = pad;
    for (auto* k : { &unison, &detune, &spread })
    {
        k->setBounds (x, top, Knob::knobWidth, Knob::knobHeight);
        x += knobPitch;
    }

    if (index == 0)
    {
        for (auto* k : { ring.get(), fm.get() })
        {
            k->setBounds (x, top, Knob::knobWidth, Knob::knobHeight);
            x += knobPitch;
        }

        const int rightX = x + 10, rightWidth = width - pad - rightX;
        fmTarget->setBounds (rightX, top + 16, rightWidth, 24);
        antiAlias.setBounds (rightX, top + 48, rightWidth, 20);
    }
    else
    {
        const int rightX = x + 14;
        antiAlias.setBounds (rightX, top + 8, 108, 20);
        sync->setBounds (rightX + 120, top + 8, width - pad - (rightX + 120), 20);
    }

    statusArea = { pad, top + Knob::knobHeight + 4, width - 2 * pad, 16 };
    statusRegion.setBounds (statusArea);
}

bool OscillatorExtras::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }

    return false;
}

void OscillatorExtras::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::lifted, p.surface);

    if (index == 0)
        drawCaption (g, "FM target", juce::Rectangle<float> ((float) fmTarget->getX(), (float) fmTarget->getY() - 14.0f, (float) fmTarget->getWidth(), 12.0f), p.ink2);

    auto area = statusArea.toFloat();

    if (statusIsWarning)
    {
        g.setColour (p.accentInk);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ area.getX() + 4.0f, area.getCentreY() }));
        area.removeFromLeft (14.0f);
    }

    g.setColour (statusIsWarning ? p.ink : p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText (status, area, juce::Justification::centredLeft, true);
}

//==============================================================================
NoiseCard::NoiseCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : colour (p.apvts, params::noiseColour, "Noise colour", p.undoManager, &Palette::src),
      level (p.apvts, params::noiseLevel, "Level", p.undoManager, "Noise level"),
      colourWatcher (*p.apvts.getParameter (params::noiseColour),
                     [this] (float v) { colourIndex = (int) std::lround (v); repaint(); })
{
    setTitle ("Noise");
    makeRemovable ("Remove Noise", [&p] { p.removeModule (plugin::Module::noise); });
    makeControllable (p, "noise", true);
    level.setModulation (model, { engine::ModDest::noiseLevel });

    addAndMakeVisible (colour);
    addAndMakeVisible (level);
    applyColours();
    colourWatcher.sendInitialUpdate();
    setFocusOrder ({ &colour, &level });
}

void NoiseCard::applyColours()
{
    level.setFamilyColour (paletteOf (*this).srcInk);
}

void NoiseCard::lookAndFeelChanged()
{
    applyColours();
}

std::unique_ptr<juce::AccessibilityHandler> NoiseCard::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group,
                                                     [this]
                                                     {
                                                         return juce::String (colourIndex == 0 ? "White noise: every frequency equally loud, a bright hiss."
                                                                                               : "Pink noise: less treble, more body, like steady rain.");
                                                     });
}

void NoiseCard::resized()
{
    constexpr int top = 4, midX = pad, blockWidth = 166, knobsX = 180;

    headerRow = { midX, top, blockWidth, 20 };
    placeRemoveButton (headerRow);
    colour.setBounds (midX, top + 25, blockWidth, 24);
    level.setBounds (knobsX, top, Knob::knobWidth, Knob::knobHeight);

    const int textX = knobsX + knobPitch + 12;
    textArea = { textX, top + 4, width - pad - textX, 60 };
}

void NoiseCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (101.0f), p.src, noiseGlyph, "Noise");

    const auto font = uiFont (12.0f);
    g.setFont (font);
    g.setColour (p.mute);
    const char* text = colourIndex == 0 ? "White noise: every frequency equally loud, a bright hiss. Try it with the filter."
                                        : "Pink noise: less treble, more body, like steady rain.";
    drawLines (g, wrapLines (font, text, (float) textArea.getWidth()), textArea.getPosition().toFloat(),
               (float) textArea.getWidth(), 16.0f);
}

//==============================================================================
FilterCard::FilterCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : curve (p),
      mode (p.apvts, params::filterMode, "Filter mode", p.undoManager, &Palette::flt),
      shaperMode (p.apvts, params::voiceShaperMode, "Voice shaper mode", p.undoManager, &Palette::flt),
      cutoff (p.apvts, params::filterCutoff, "Cutoff", p.undoManager, "Filter cutoff"),
      resonance (p.apvts, params::filterResonance, "Reso", p.undoManager, "Filter resonance"),
      keyTrack (p.apvts, params::filterKeyTrack, "Key track", p.undoManager, "Filter key tracking"),
      drive (p.apvts, params::filterDrive, "Drive", p.undoManager, "Filter drive"),
      shaperDrive (p.apvts, params::voiceShaperDrive, "Shaper drive", p.undoManager, "Per-voice shaper drive"),
      shaperBias (p.apvts, params::voiceShaperBias, "Bias", p.undoManager, "Per-voice shaper bias"),
      shaperMix (p.apvts, params::voiceShaperMix, "Shaper mix", p.undoManager, "Per-voice shaper mix")
{
    shaperEnabled = std::make_unique<ToggleChip> (p.apvts, params::voiceShaperEnabled, "Shape", "Per-voice shaper", p.undoManager, &Palette::flt);
    makeRemovable ("Remove Filter", [&p] { p.removeModule (plugin::Module::filter); });
    makeControllable (p, "filter", false);

    // The filter envelope's own amount is a connection too, so it shows as an arc on the cutoff.
    cutoff.setModulation (model, { engine::ModDest::cutoff }, p.apvts.getParameter (params::filterEnvAmount));
    resonance.setModulation (model, { engine::ModDest::resonance });

    for (auto* c : std::initializer_list<juce::Component*> { &curve, &mode, &cutoff, &resonance, &keyTrack, &drive, shaperEnabled.get(), &shaperMode, &shaperDrive, &shaperBias, &shaperMix })
        if (c != nullptr)
            addAndMakeVisible (c);

    applyColours();
    setFocusOrder ({ &mode, &cutoff, &resonance, &keyTrack, &drive, shaperEnabled.get(), &shaperMode, &shaperDrive, &shaperBias, &shaperMix, &curve });
}

void FilterCard::applyColours()
{
    const auto ink = paletteOf (*this).fltInk;
    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive, &shaperDrive, &shaperBias, &shaperMix })
        k->setFamilyColour (ink);
}

void FilterCard::lookAndFeelChanged()
{
    applyColours();
}

void FilterCard::resized()
{
    auto inner = getLocalBounds().reduced (pad, 0);

    headerRow = inner.withTrimmedTop (pad).withHeight (headerHeight);
    placeRemoveButton (headerRow);
    curve.setBounds (inner.getX(), 32, inner.getWidth(), 50);
    mode.setBounds (inner.getX(), 88, inner.getWidth(), 24);
    shaperEnabled->setBounds (inner.getX(), 114, 70, 22);
    shaperMode.setBounds (inner.getX() + 74, 114, inner.getWidth() - 74, 22);

    constexpr int knobs = 4;
    const int gap = (inner.getWidth() - knobs * Knob::knobWidth) / (knobs - 1);
    int x = inner.getX();
    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive })
    {
        k->setBounds (x, 142, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + gap;
    }

    x = inner.getX();
    for (auto* k : { &shaperDrive, &shaperBias, &shaperMix })
    {
        k->setBounds (x, 196, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + 12;
    }
}

void FilterCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (76.0f), p.flt, filterGlyph, "Filter");
}

//==============================================================================
EnvelopeCard::EnvelopeCard (plugin::ScratchpadProcessor& p, ModulationModel& model, plugin::Module which)
    : isAmp (which == plugin::Module::ampEnv),
      display (p, model, which == plugin::Module::ampEnv),
      attack (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).attack, "Attack", p.undoManager,
              plugin::ScratchpadProcessor::moduleName (which) + " attack"),
      decay (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).decay, "Decay", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + " decay"),
      sustain (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).sustain, "Sustain", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " sustain"),
      release (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).release, "Release", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " release"),
      extra (p.apvts, isAmp ? params::ampEnvVelocity : params::filterEnvAmount, isAmp ? "Velocity" : "Amount", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + (isAmp ? " velocity sensitivity" : " amount"))
{
    setTitle (plugin::ScratchpadProcessor::moduleName (which));
    makeRemovable ("Remove " + plugin::ScratchpadProcessor::moduleName (which), [&p, which] { p.removeModule (which); });
    makeControllable (p, isAmp ? "ampEnv" : "filterEnv", false);

    addAndMakeVisible (display);

    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
        addAndMakeVisible (k);

    applyColours();
    setFocusOrder ({ &attack, &decay, &sustain, &release, &extra, &display });
}

void EnvelopeCard::applyColours()
{
    const auto& p = paletteOf (*this);
    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
        k->setFamilyColour (isAmp ? p.ampInk : p.fltInk);
}

void EnvelopeCard::lookAndFeelChanged()
{
    applyColours();
}

void EnvelopeCard::resized()
{
    auto inner = getLocalBounds().reduced (pad, 0);

    headerRow = inner.withTrimmedTop (pad).withHeight (headerHeight);
    placeRemoveButton (headerRow);
    display.setBounds (inner.getX(), 32, inner.getWidth(), 36);

    constexpr int knobs = 5;
    const int gap = (inner.getWidth() - knobs * Knob::knobWidth) / (knobs - 1);
    int x = inner.getX();
    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
    {
        k->setBounds (x, 72, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + gap;
    }
}

void EnvelopeCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (76.0f), isAmp ? p.amp : p.flt, envelopeGlyph,
                isAmp ? "Amp Envelope" : "Filter Envelope");
}

//==============================================================================
AmpCard::AmpCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : processor (p),
      level (model, engine::ModDest::ampLevel, "Level", "Amp level", 100.0f,
             [] (float percent) { return juce::jlimit (0.0f, 1.0f, percent / 100.0f); }, "100%"),
      pan (model, engine::ModDest::pan, "Pan", "Amp pan", 0.0f,
           [] (float percent) { return juce::jlimit (0.0f, 1.0f, (percent + 100.0f) / 200.0f); }, "C")
{
    setTitle ("Amp");
    setExplainKey ("amp");
    addAndMakeVisible (level);
    addAndMakeVisible (pan);
    setFocusOrder ({ &level, &pan });
}

std::unique_ptr<juce::AccessibilityHandler> AmpCard::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group,
                                                     [this]
                                                     {
                                                         return juce::String (processor.isModulePresent (plugin::Module::ampEnv)
                                                                                  ? "The amp envelope sets how each note starts and ends."
                                                                                  : "A fixed 2 ms fade on every note start and stop.");
                                                     });
}

void AmpCard::resized()
{
    constexpr int dialsTop = 26;
    const int panX = width - pad - Knob::knobWidth;
    const int levelX = panX - Knob::knobWidth - 4;

    level.setBounds (levelX, dialsTop, Knob::knobWidth, Knob::knobHeight);
    pan.setBounds (panX, dialsTop, Knob::knobWidth, Knob::knobHeight);
    textArea = { pad, 52, levelX - pad - 6, height - 52 - 4 };
}

void AmpCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    const bool shaped = processor.isModulePresent (plugin::Module::ampEnv);

    drawHeader (g, p, juce::Rectangle<float> ((float) pad, (float) pad - 4.0f, (float) width - 2.0f * pad, (float) headerHeight),
                p.amp, shaped ? envelopeGlyph : gateGlyph, "Amp");

    drawCaption (g, shaped ? "Envelope" : "On / off gate",
                 juce::Rectangle<float> ((float) pad, 30.0f, (float) level.getX() - pad - 4.0f, 13.0f), p.ink2);

    g.setColour (p.mute);
    const auto font = uiFont (11.0f);
    g.setFont (font);
    const char* text = shaped ? "The envelope sets how each note starts and ends."
                              : "A fixed 2 ms fade on every note start and stop.";
    drawLines (g, wrapLines (font, text, (float) textArea.getWidth()), textArea.getPosition().toFloat(),
               (float) textArea.getWidth(), 14.0f);
}

//==============================================================================
VoicingCard::VoicingCard (plugin::ScratchpadProcessor& p)
    : mode (p.apvts, params::voicingMode, "Voice mode", p.undoManager, &Palette::perf),
      voices (p.apvts, params::voicingVoices, "Voices", p.undoManager, "Voices"),
      glide (p.apvts, params::voicingGlide, "Glide", p.undoManager, "Glide time"),
      bendRange (p.apvts, params::voicingBendRange, "Bend", p.undoManager, "Pitch bend range"),
      drift (p.apvts, params::voicingDrift, "Drift", p.undoManager, "Analogue pitch and cutoff drift"),
      modeWatcher (*p.apvts.getParameter (params::voicingMode),
                   [this] (float v)
                   {
                       modeIndex = (int) std::lround (v);
                       voices.setEnabled (modeIndex == 0); // mono and legato are one voice
                       repaint (headerRow);
                   })
{
    setTitle ("Voicing");
    setExplainKey ("voicing");

    for (auto* c : std::initializer_list<juce::Component*> { &mode, &voices, &glide, &bendRange, &drift })
        addAndMakeVisible (c);

    applyColours();
    modeWatcher.sendInitialUpdate();
    voices.setDisabledNote ("Not in use: mono and legato play one voice.");
    setFocusOrder ({ &mode, &voices, &glide, &bendRange, &drift });
}

void VoicingCard::applyColours()
{
    const auto ink = paletteOf (*this).perfInk;
    for (auto* k : { &voices, &glide, &bendRange, &drift })
        k->setFamilyColour (ink);
}

void VoicingCard::lookAndFeelChanged()
{
    applyColours();
}

void VoicingCard::resized()
{
    constexpr int top = 6, titleWidth = 74;

    headerRow = { pad, top, titleWidth, 24 };
    mode.setBounds (pad + titleWidth, top, width - 2 * pad - titleWidth, 24);

    constexpr int knobs = 4;
    const int gap = (width - 2 * pad - knobs * Knob::knobWidth) / (knobs - 1);
    int x = pad;
    for (auto* k : { &voices, &glide, &bendRange, &drift })
    {
        k->setBounds (x, 36, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + gap;
    }
}

void VoicingCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    g.setColour (p.ink);
    auto font = uiFont (13.0f, Weight::medium);
    font.setExtraKerningFactor (0.08f);
    g.setFont (font);
    g.drawText ("Voicing", headerRow, juce::Justification::centredLeft, false);
}

//==============================================================================
OutputCard::OutputCard (plugin::ScratchpadProcessor& p)
    : volume (p.apvts, params::outVolume, "Volume", p.undoManager, "Output volume")
{
    setTitle ("Output");
    setExplainKey ("output");
    addAndMakeVisible (volume);
    volume.setFamilyColour (paletteOf (*this).perfInk);
    setFocusOrder ({ &volume });
}

void OutputCard::lookAndFeelChanged()
{
    volume.setFamilyColour (paletteOf (*this).perfInk);
}

void OutputCard::resized()
{
    volume.setBounds ((getWidth() - Knob::knobWidth) / 2, 36, Knob::knobWidth, Knob::knobHeight);
}

void OutputCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, juce::Rectangle<float> ((float) pad + 2.0f, (float) pad, (float) width - 2.0f * pad, (float) headerHeight),
                p.perf, outputGlyph, "Out");
}

} // namespace scratchpad::ui
