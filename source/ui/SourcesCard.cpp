#include "ui/SourcesCard.h"

#include "dsp/Oscillator.h"
#include "plugin/Parameters.h"
#include "ui/Neumorph.h"
#include "ui/Typography.h"
#include "ui/WaveGlyphs.h"

#include <cmath>
#include <utility>

namespace scratchpad::ui
{

using dsp::Wave;

namespace
{
    juce::String oscName (int index) { return "Oscillator " + juce::String (index + 1); }
    juce::String oscShortName (int index) { return "Osc " + juce::String (index + 1); }
    const params::OscIds& oscIds (int index) { return params::osc[(size_t) index]; }

    const juce::String arrow { juce::CharPointer_UTF8 (" \xe2\x86\x92 ") };

    // The table cell has room for "Table A", not "Wavetable A".
    juce::String cellWaveName (int wave)
    {
        return wave >= (int) Wave::wavetableA ? "Table " + juce::String::charToString ((juce::juce_wchar) ('A' + wave - (int) Wave::wavetableA))
                                              : params::waveNames()[wave];
    }

    constexpr int cellGap = 2;       // between the cells of a row
    constexpr int bodyX = SourceRow::nameColumn + SourceRow::columnGap;
    constexpr int knobHeight = KnobGeometry::heightFor (layout::knobs::table);
    constexpr int chromeHeight = 16 + 5 + 18; // name line, gap, the B and S row

    constexpr int tableTop = layout::padding::sources + layout::table::headingHeight + layout::table::headingMargin;

    // An oscillator row, right to left: More, then the five knob columns, then the Wave well takes what is left.
    constexpr int knobsWidth = 5 * layout::table::cell + 4 * cellGap;
    constexpr int oscillatorMoreInset = SourceRow::columnWidth - (bodyX + layout::table::waveWidth + cellGap + knobsWidth + SourceRow::columnGap);

    /** The Wave column's width in a row `rowWidth` wide: 52 at the compact width. */
    constexpr int waveColumnWidth (int rowWidth) noexcept
    {
        return rowWidth - oscillatorMoreInset - SourceRow::columnGap - knobsWidth - cellGap - bodyX;
    }
}

//==============================================================================
WaveChooser::WaveChooser (const juce::String& accessibleName, std::function<juce::String()> describeFn)
    : describe (std::move (describeFn))
{
    setTitle (accessibleName);
    setWantsKeyboardFocus (true);
    setPaintingIsUnclipped (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveChooser::setWave (int newWave)
{
    newWave = juce::jlimit (0, dsp::numWaves - 1, newWave);

    if (newWave == wave)
        return;

    wave = newWave;
    repaint();

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void WaveChooser::setHovered (bool shouldBeHovered)
{
    if (hovered != shouldBeHovered)
    {
        hovered = shouldBeHovered;
        repaint();
    }
}

void WaveChooser::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    // The well takes the chooser's height less the name line under it (14 px), and all of its width.
    const float wellHeight = (float) (getHeight() - (layout::table::waveHeight - layout::table::waveWellHeight));
    const auto well = juce::Rectangle<float> ((float) getWidth(), wellHeight);

    drawInset (g, well, 9.0f, p, p.well);

    if (hovered)
    {
        g.setColour (p.ink.withAlpha (0.05f));
        g.fillRoundedRectangle (well, 9.0f);
    }

    const auto drawArea = well.reduced (3.0f, 0.0f);
    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (p.track);
    g.drawDashedLine ({ drawArea.getX(), drawArea.getCentreY(), drawArea.getRight(), drawArea.getCentreY() }, dashes, 2, 1.0f);
    g.setColour (p.srcInk);
    g.strokePath (wavePath ((Wave) wave, drawArea, 2.0f, 0.64f), juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    g.drawText (cellWaveName (wave), juce::Rectangle<float> (0.0f, wellHeight + 2.0f, (float) getWidth(), 12.0f),
                juce::Justification::centred, false);

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), 9.0f, p.ink);
}

void WaveChooser::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && contains (e.getPosition()))
        showMenu();
}

bool WaveChooser::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isAnyModifierKeyDown())
        return false;

    const int code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey)
    {
        showMenu();
        return true;
    }

    const bool back = code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey;
    const bool forward = code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey;

    if (! back && ! forward)
        return false;

    const int next = wave + (forward ? 1 : -1);

    if (next >= 0 && next < dsp::numWaves && onPick)
        onPick (next);

    return true;
}

void WaveChooser::showMenu()
{
    juce::PopupMenu menu;
    const auto names = params::waveNames();

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i], true, i == wave);

    showStyledMenu (menu, juce::PopupMenu::Options().withTargetComponent (this),
                        [safe = juce::Component::SafePointer<WaveChooser> (this)] (int result)
                        {
                            if (safe != nullptr && result >= 1 && safe->onPick)
                                safe->onPick (result - 1);
                        });
}

std::unique_ptr<juce::AccessibilityHandler> WaveChooser::createAccessibilityHandler()
{
    return a11y::makeSelectorHandler (
        *this, [this] { return params::waveNames()[wave]; },
        [this] (const juce::String& text)
        {
            const int found = params::waveNames().indexOf (text.trim(), true);

            if (found >= 0 && onPick)
                onPick (found);
        },
        describe);
}

//==============================================================================
SourceRow::SourceRow (plugin::ScratchpadProcessor& p, const juce::String& title, const juce::String& shortNameToDraw, const juce::String& key)
    : processor (p), shortName (shortNameToDraw)
{
    setTitle (title);
    makeControllable (p, key, true);
}

void SourceRow::adoptChrome()
{
    for (auto* child : getChildren())
    {
        auto* toggle = dynamic_cast<HeaderToggle*> (child);

        if (toggle == nullptr)
            continue;

        if (toggle->getTitle() == "Explain " + getTitle())
            explain = toggle;
        else if (toggle->getTitle() == getTitle() + " bypass")
            bypass = toggle;
        else if (toggle->getTitle() == getTitle() + " solo")
            solo = toggle;
    }

    jassert (explain != nullptr && bypass != nullptr && solo != nullptr);
}

void SourceRow::layoutChrome (int rowHeight, int moreX)
{
    namespace h = layout::header;

    height = rowHeight;
    const int top = 1 + (rowHeight - 1 - chromeHeight) / 2;
    const int nameWidth = (int) std::ceil (textWidth (uiFont (h::nameSize, Weight::medium), shortName));

    explain->setBounds (nameWidth + h::gap, top + 1, h::explain, h::explain);
    bypass->setBounds (0, top + 21, h::buttonWidth, 18);
    solo->setBounds (h::buttonWidth + h::buttonGap, top + 21, h::buttonWidth, 18);

    if (auto* more = getMoreButton())
        more->setBounds (moreX, (rowHeight - h::buttonHeight) / 2, h::buttonWidth, h::buttonHeight);
}

void SourceRow::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    g.setColour (p.line);
    g.fillRect (0, 0, getWidth(), 1);

    g.setColour (p.ink);
    g.setFont (uiFont (layout::header::nameSize, Weight::medium));
    const int top = 1 + (height - 1 - chromeHeight) / 2;
    g.drawText (shortName, juce::Rectangle<int> (0, top, nameColumn, 16), juce::Justification::centredLeft, false);
}

//==============================================================================
OscillatorCard::OscillatorCard (plugin::ScratchpadProcessor& p, ModulationModel& model, int oscillatorIndex)
    : SourceRow (p, oscName (oscillatorIndex), oscShortName (oscillatorIndex), "osc" + juce::String (oscillatorIndex + 1)),
      index (oscillatorIndex),
      waveChooser (oscName (oscillatorIndex) + " wave",
                   [this]
                   {
                       return params::waveNames()[wave] + " wave"
                              + (aliasingOff ? ". Anti-aliasing is off, so expect harsh extra tones." : juce::String());
                   }),
      octave (p.apvts, oscIds (oscillatorIndex).octave, "Oct", p.undoManager, oscName (oscillatorIndex) + " octave", layout::knobs::table),
      semitone (p.apvts, oscIds (oscillatorIndex).semitone, "Semi", p.undoManager, oscName (oscillatorIndex) + " semitone", layout::knobs::table),
      fine (p.apvts, oscIds (oscillatorIndex).fine, "Fine", p.undoManager, oscName (oscillatorIndex) + " fine tune", layout::knobs::table),
      level (p.apvts, oscIds (oscillatorIndex).level, "Level", p.undoManager, oscName (oscillatorIndex) + " level", layout::knobs::table),
      morph (p.apvts, oscIds (oscillatorIndex).pulseWidth, "Morph", p.undoManager, oscName (oscillatorIndex) + " pulse width or wave morph",
             layout::knobs::table),
      waveAttachment (*p.apvts.getParameter (oscIds (oscillatorIndex).wave), [this] (float v) { setWave ((int) std::lround (v)); }, &p.undoManager)
{
    const auto& ids = oscIds (index);

    waveChooser.getProperties().set ("explainId", ids.wave);
    waveChooser.onPick = [this] (int picked)
    {
        if (index == 0)
        {
            processor.changeWave (picked);
            return;
        }

        const auto names = params::waveNames();
        const auto before = names[wave];
        waveAttachment.setValueAsCompleteGesture ((float) picked);
        processor.undoManager.setCurrentTransactionName (oscName (index) + " wave " + before + arrow + names[picked]);
    };

    const bool removable = index > 0;
    const auto which = index == 1 ? plugin::Module::osc2 : plugin::Module::osc3;

    makeMore (oscShortName (index), &Palette::src, [this] (MorePopover& popover) { fillMore (popover); },
              removable ? "Remove " + oscShortName (index) : juce::String(), removable ? "Remove " + oscName (index) : juce::String(),
              removable ? std::function<void()> ([&p, which] { p.removeModule (which); }) : std::function<void()>());
    adoptChrome();

    for (auto* k : { &octave, &semitone, &fine })
        k->setBipolar (true);

    // Fine tune is the pitch knob: a drop there moves this oscillator's pitch, or all of them.
    fine.setModulation (model, index == 0 ? std::vector<engine::ModDest> { engine::ModDest::pitchAll, engine::ModDest::pitch1 }
                                          : std::vector<engine::ModDest> { engine::pitchDest ((size_t) index), engine::ModDest::pitchAll });
    level.setModulation (model, { engine::levelDest ((size_t) index) });
    morph.setModulation (model, { engine::pulseWidthDest ((size_t) index) });

    for (auto* c : std::initializer_list<juce::Component*> { &waveChooser, &octave, &semitone, &fine, &level, &morph })
        addAndMakeVisible (c);

    for (auto* k : { &octave, &semitone, &fine, &level, &morph })
        k->setShowsValueOnly();

    setFocusOrder ({ &waveChooser, &octave, &semitone, &fine, &level, &morph });

    // The More button's dot follows the pop-over's parameters, however they change.
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
    waveAttachment.sendInitialUpdate();
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
    const auto& ids = oscIds (index);

    aliasingOff = raw (ids.antiAlias) < 0.5f;

    bool inUse = raw (ids.unison) > 1.5f;

    if (ids.sync != nullptr)
        inUse = inUse || raw (ids.sync) > 0.5f;

    if (index == 0)
        inUse = inUse || raw (params::ringLevel) > params::ringLevelDefaultDb + 0.5f || raw (params::fmAmount) > 0.5f;

    if (auto* more = getMoreButton())
        more->setMarker (aliasingOff ? MoreButton::Marker::warning : inUse ? MoreButton::Marker::inUse : MoreButton::Marker::none);
}

juce::String OscillatorCard::statusSentence() const
{
    if (raw (oscIds (index).antiAlias) < 0.5f)
        return "Anti-aliasing is off: expect harsh, glassy extra tones, most of all on high notes.";

    if (index != 0)
        return {};

    // Ring and FM work on other oscillators: with none to work on, they dim and this line says why.
    const bool hasOsc2 = processor.isModulePresent (plugin::Module::osc2), hasOsc3 = processor.isModulePresent (plugin::Module::osc3);
    const int target = (int) std::lround (raw (params::fmTarget));
    const bool targetPresent = target == 0 ? hasOsc2 : target == 1 ? hasOsc3 : (hasOsc2 || hasOsc3);

    if (! hasOsc2 && ! hasOsc3)
        return "Ring modulation and FM act on Oscillator 2 or 3: add one to hear them.";

    if (! hasOsc2 && raw (params::ringLevel) > params::ringLevelDefaultDb + 0.5f)
        return "Ring modulation needs Oscillator 2 in the patch.";

    if (! targetPresent)
        return "FM has no target in the patch: pick another, or add that oscillator.";

    return {};
}

void OscillatorCard::fillMore (MorePopover& popover)
{
    const auto& ids = oscIds (index);
    const auto name = oscName (index);

    popover.addSwitch (processor, ids.phaseReset, "Phase reset on each note", name + " phase reset");
    popover.addKnobs (processor, "Unison",
                      { { ids.unison, "Copies", name + " unison voices", {} },
                        { ids.unisonDetune, "Detune", name + " unison detune", {} },
                        { ids.unisonSpread, "Spread", name + " unison spread", {} },
                        { ids.subLevel, "Sub", name + " sub-oscillator level", {} } });

    if (index == 0)
    {
        popover.addKnobs (processor, "Osc 1 shapes the others",
                          { { params::fmAmount, "FM", "FM amount", {} }, { params::ringLevel, "Ring", "Ring modulation level", {} } },
                          "Ring mod multiplies Osc 1 by Osc 2 and adds the result as its own source.");
        popover.addSegments (processor, params::fmTarget, "FM target", "FM target", 3);

        const bool hasOsc2 = processor.isModulePresent (plugin::Module::osc2), hasOsc3 = processor.isModulePresent (plugin::Module::osc3);
        const int target = (int) std::lround (raw (params::fmTarget));
        const bool targetPresent = target == 0 ? hasOsc2 : target == 1 ? hasOsc3 : (hasOsc2 || hasOsc3);

        const auto dim = [&popover] (const char* id, bool usable, const juce::String& why)
        {
            if (auto* knob = dynamic_cast<Knob*> (popover.findControl (id)))
            {
                knob->setDisabledNote (why);
                knob->setEnabled (usable);
            }
        };

        dim (params::ringLevel, hasOsc2, "Not in use: it needs Oscillator 2 in the patch.");
        dim (params::fmAmount, targetPresent, "Not in use: its target oscillator is not in the patch.");
    }
    else
    {
        popover.addSwitch (processor, ids.sync, "Hard sync to Osc 1", name + " hard sync", "Restarts this wave whenever Osc 1 starts a cycle.");
    }

    popover.addSwitch (processor, ids.antiAlias, "Anti-aliasing", name + " anti-aliasing");

    if (const auto status = statusSentence(); status.isNotEmpty())
        popover.addNote (status);
}

void OscillatorCard::applyColours()
{
    const auto ink = paletteOf (*this).srcInk;

    for (auto* k : { &octave, &semitone, &fine, &level, &morph })
        k->setFamilyColour (ink);
}

void OscillatorCard::lookAndFeelChanged()
{
    applyColours();
}

void OscillatorCard::setWave (int newWave)
{
    wave = juce::jlimit (0, dsp::numWaves - 1, newWave);
    waveChooser.setWave (wave);

    // Pulse width (the pulse wave) and the table position (the wavetables) are the same parameter; a saw has neither.
    morph.setVisible (wave == (int) Wave::pulse || wave >= (int) Wave::wavetableA);
}

void OscillatorCard::resized()
{
    const int rowHeight = getHeight(), extra = juce::jmax (0, rowHeight - OscillatorCard::rowHeight);

    // The five knob columns keep their size and stay against More; the wave well takes the extra width and height.
    const int moreX = getWidth() - oscillatorMoreInset;
    layoutChrome (rowHeight, moreX);

    const int top = 1 + (rowHeight - 1 - knobHeight) / 2;
    const int knobsX = moreX - columnGap - knobsWidth;
    waveChooser.setBounds (bodyX, top - extra / 2, knobsX - cellGap - bodyX, layout::table::waveHeight + extra);

    int x = knobsX;

    for (auto* k : { &octave, &semitone, &fine, &level, &morph })
    {
        k->setBoundsAt (x, top);
        x += layout::table::cell + cellGap;
    }
}

//==============================================================================
NoiseCard::NoiseCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : SourceRow (p, "Noise", "Noise", "noise"),
      colour (p.apvts, params::noiseColour, "Noise colour", p.undoManager, &Palette::src),
      level (p.apvts, params::noiseLevel, "Level", p.undoManager, "Noise level", layout::knobs::noiseLevel),
      colourWatcher (*p.apvts.getParameter (params::noiseColour), [this] (float v) { colourIndex = (int) std::lround (v); refreshText(); })
{
    makeMore ("Noise", &Palette::src, {}, "Remove noise", "Remove Noise", [&p] { p.removeModule (plugin::Module::noise); });
    adoptChrome();

    level.setModulation (model, { engine::ModDest::noiseLevel });

    addAndMakeVisible (colour);
    addAndMakeVisible (level);
    addAndMakeVisible (textRegion);
    level.setShowsValueOnly();
    setFocusOrder ({ &colour, &level });

    applyColours();
    colourWatcher.sendInitialUpdate();
}

void NoiseCard::refreshText()
{
    textRegion.setText (colourIndex == 0 ? "White noise: every frequency equally loud, a bright hiss."
                                         : "Pink noise: less treble, more body, like steady rain.");
    repaint();
}

void NoiseCard::applyColours()
{
    level.setFamilyColour (paletteOf (*this).srcInk);
}

void NoiseCard::lookAndFeelChanged()
{
    applyColours();
}

void NoiseCard::resized()
{
    const int rowHeight = getHeight();
    const int moreX = getWidth() - layout::header::buttonWidth;
    layoutChrome (rowHeight, moreX);

    // The colour segments and the level knob keep their size; Level stays against More, the sentence takes the rest.
    const int bodyRight = moreX - columnGap;
    const int levelX = bodyRight - layout::knobs::noiseLevel.cell;
    constexpr int segmentsHeight = 24;

    colour.setBounds (bodyX, 1 + (rowHeight - 1 - segmentsHeight) / 2, layout::segments::noiseColour, segmentsHeight);
    level.setBoundsAt (levelX, 1 + (rowHeight - 1 - knobHeight) / 2);

    const int textX = bodyX + layout::segments::noiseColour + columnGap;
    textArea = { textX, 1, levelX - columnGap - textX, rowHeight - 1 };
    textRegion.setBounds (textArea);
}

void NoiseCard::paint (juce::Graphics& g)
{
    SourceRow::paint (g);

    const auto& p = paletteOf (*this);
    const auto font = uiFont (11.0f);
    const char* text = colourIndex == 0 ? "Every frequency equally loud: a bright hiss." : "Less treble, more body: like steady rain.";
    const auto lines = wrapLines (font, text, (float) textArea.getWidth());
    constexpr float lineHeight = 14.0f;
    const float blockHeight = lineHeight * (float) lines.size();

    g.setFont (font);
    g.setColour (p.mute);
    drawLines (g, lines, { (float) textArea.getX(), (float) textArea.getCentreY() - blockHeight * 0.5f }, (float) textArea.getWidth(), lineHeight);
}

//==============================================================================
SourcesCard::SourcesCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : processor (p),
      osc1 (p, model, 0), osc2 (p, model, 1), osc3 (p, model, 2), noise (p, model),
      osc2Slot ("Add Oscillator 2", "Osc 2", SlotButton::Style::chip),
      osc3Slot ("Add Oscillator 3", "Osc 3", SlotButton::Style::chip),
      noiseSlot ("Add Noise", "Noise", SlotButton::Style::chip)
{
    setTitle ("Sources");
    setFocusContainerType (juce::Component::FocusContainerType::focusContainer);

    for (auto* c : std::initializer_list<juce::Component*> { &osc1, &osc2, &osc2Slot, &osc3, &osc3Slot, &noise, &noiseSlot })
        addAndMakeVisible (c);

    const struct { SlotButton* slot; plugin::Module which; } slots[] = {
        { &osc2Slot, plugin::Module::osc2 }, { &osc3Slot, plugin::Module::osc3 }, { &noiseSlot, plugin::Module::noise }
    };

    for (const auto& s : slots)
    {
        s.slot->getProperties().set ("explainId", "control.addModule");
        s.slot->onClick = [this, which = s.which] { if (onAddModule) onAddModule (which); };
    }

    refreshPresence();
}

bool SourcesCard::holds (plugin::Module m) noexcept
{
    return m == plugin::Module::osc2 || m == plugin::Module::osc3 || m == plugin::Module::noise;
}

void SourcesCard::refreshPresence()
{
    const struct { juce::Component* row; juce::Component* slot; plugin::Module which; } optional[] = {
        { &osc2, &osc2Slot, plugin::Module::osc2 }, { &osc3, &osc3Slot, plugin::Module::osc3 }, { &noise, &noiseSlot, plugin::Module::noise }
    };

    for (const auto& o : optional)
    {
        const bool present = processor.isModulePresent (o.which);
        o.row->setVisible (present);
        o.slot->setVisible (! present);
    }

    repaint();
}

ModuleCard* SourcesCard::rowFor (plugin::Module m)
{
    if (! holds (m) || ! processor.isModulePresent (m))
        return nullptr;

    return m == plugin::Module::osc2 ? static_cast<ModuleCard*> (&osc2) : m == plugin::Module::osc3 ? static_cast<ModuleCard*> (&osc3) : &noise;
}

juce::Component* SourcesCard::slotFor (plugin::Module m)
{
    if (! holds (m) || processor.isModulePresent (m))
        return nullptr;

    return m == plugin::Module::osc2 ? &osc2Slot : m == plugin::Module::osc3 ? &osc3Slot : &noiseSlot;
}

void SourcesCard::focusModule (plugin::Module m)
{
    if (auto* row = rowFor (m))
        row->focusFirstControl();
}

void SourcesCard::focusSlot (plugin::Module m)
{
    if (auto* slot = slotFor (m))
        slot->grabKeyboardFocus();
}

std::vector<juce::Component*> SourcesCard::focusOrder()
{
    std::vector<juce::Component*> order { &osc1 };

    for (const auto m : { plugin::Module::osc2, plugin::Module::osc3, plugin::Module::noise })
        order.push_back (rowFor (m) != nullptr ? static_cast<juce::Component*> (rowFor (m)) : slotFor (m));

    return order;
}

std::unique_ptr<juce::AccessibilityHandler> SourcesCard::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group,
                                                     [] { return juce::String ("The oscillators and the noise: where the raw sound comes from."); });
}

juce::Rectangle<int> SourcesCard::rowArea (int row) const
{
    using namespace layout;

    // The four rows share the extra height equally; any remainder pixels go to the top rows.
    const int extra = juce::jmax (0, getHeight() - sources.h);
    const auto heightOf = [extra] (int r) { return (r < 3 ? table::rowHeight : NoiseCard::rowHeight) + extra / 4 + (r < extra % 4 ? 1 : 0); };

    int y = tableTop;

    for (int r = 0; r < row; ++r)
        y += heightOf (r);

    return { padding::sources, y, getWidth() - 2 * padding::sources, heightOf (row) };
}

juce::Rectangle<int> SourcesCard::slotArea (int row) const
{
    constexpr int slotHeight = 40;
    const auto area = rowArea (row);
    const int extra = area.getHeight() - (row < 3 ? layout::table::rowHeight : NoiseCard::rowHeight);

    return area.withTrimmedTop (1).withSizeKeepingCentre (area.getWidth(), slotHeight + extra);
}

void SourcesCard::resized()
{
    osc1.setBounds (rowArea (0));
    osc2.setBounds (rowArea (1));
    osc3.setBounds (rowArea (2));
    noise.setBounds (rowArea (3));

    osc2Slot.setBounds (slotArea (1));
    osc3Slot.setBounds (slotArea (2));
    noiseSlot.setBounds (slotArea (3));
}

void SourcesCard::paint (juce::Graphics& g)
{
    using namespace layout;

    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::small, p.surface);

    // Headings: the table's labels. Values live in the cells, so a knob never repeats its heading.
    const float y = (float) padding::sources;
    const float h = (float) table::headingHeight;
    drawCaption (g, "Sources", { (float) padding::sources, y, (float) table::nameColumn + 14.0f, h }, p.ink2);

    // The Wave column takes the extra width, so the other headings sit over their knobs, right-aligned with the rows.
    const int waveWidth = waveColumnWidth (getWidth() - 2 * padding::sources);
    float x = (float) (padding::sources + table::nameColumn + 6);
    drawCaption (g, "Wave", { x, y, (float) waveWidth, h }, p.mute, juce::Justification::centred, 0.06f);
    x += (float) (waveWidth + 2);

    for (const char* heading : { "Oct", "Semi", "Fine", "Level", "Morph" })
    {
        drawCaption (g, heading, { x - 6.0f, y, (float) table::cell + 12.0f, h }, p.mute, juce::Justification::centred, 0.06f);
        x += (float) (table::cell + 2);
    }

    // A slot stands where its row is not: the row's own hairline is not there to draw.
    for (const auto m : { plugin::Module::osc2, plugin::Module::osc3, plugin::Module::noise })
    {
        if (processor.isModulePresent (m))
            continue;

        const auto area = rowArea (m == plugin::Module::osc2 ? 1 : m == plugin::Module::osc3 ? 2 : 3);
        g.setColour (p.line);
        g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
    }
}

} // namespace scratchpad::ui
