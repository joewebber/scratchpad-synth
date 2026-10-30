#include "ui/ModulationStrip.h"

#include "plugin/Parameters.h"

#include <cmath>

namespace scratchpad::ui
{

using engine::ModSource;
namespace info = plugin::modulation;

namespace
{
    // 18 x 10 glyphs for the shape selector, in `dsp::LfoShape` order.
    std::vector<const char*> shapeGlyphs()
    {
        return { "M1 5c2-5.5 5-5.5 8 0s6 5.5 8 0",     // sine
                 "M1 5l4-4 8 8 4-4",                   // triangle
                 "M1 9L9 1V9l8-8",                     // saw up
                 "M1 1l8 8V1l8 8",                     // saw down
                 "M1 9V1h8v8h8V1",                     // square
                 "M1 7h3V3h3v5h3V2h3v6h3V4" };         // sample and hold
    }

    /** What the picture of an LFO shows at `x` (0 to 1 across two cycles). Sample and hold is four steps. */
    float shapeAt (dsp::LfoShape shape, double x)
    {
        if (shape == dsp::LfoShape::sampleHold)
        {
            static constexpr float steps[] = { 0.5f, -0.6f, 0.7f, -0.25f };
            return steps[juce::jlimit (0, 3, (int) (x * 4.0))];
        }

        dsp::Lfo lfo;
        lfo.setShape (shape);
        lfo.reset (std::fmod (x * 2.0, 1.0));
        return lfo.value();
    }

    juce::String sourceCaption (ModSource source)
    {
        if (source == ModSource::ampEnv)
            return "Amp env";
        if (source == ModSource::filterEnv)
            return "Filter env";

        return info::sourceName (source);
    }
}

//==============================================================================
SourceHandle::SourceHandle (ModulationModel& m, ModSource s)
    : model (m), source (s)
{
    setTitle (info::sourceName (s) + " modulation handle");
    getProperties().set ("explainId", "control.handle");
    setWantsKeyboardFocus (true);
    setPaintingIsUnclipped (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    model.addListener (this);
}

SourceHandle::~SourceHandle()
{
    model.removeListener (this);
}

juce::Point<int> SourceHandle::centreOnScreen() const
{
    return localPointToGlobal (getLocalBounds().getCentre());
}

bool SourceHandle::isConnectingFromHere() const
{
    return model.isConnecting() && model.getConnectingSource() == source;
}

void SourceHandle::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto disc = juce::Rectangle<float> ((float) diameter, (float) diameter).withCentre (getLocalBounds().getCentre().toFloat());

    if (! isEnabled())
        g.beginTransparencyLayer (0.4f);

    drawRaised (g, disc, disc.getWidth() * 0.5f, p, isConnectingFromHere() ? Elevation::lifted : Elevation::small, sourceFill (p, source));

    g.setColour (p.onAccent);
    g.setFont (uiFont (11.0f, Weight::medium));
    g.drawText (info::sourceLabel (source), disc, juce::Justification::centred, false);

    if (! isEnabled())
        g.endTransparencyLayer();

    // Armed or being dragged: a ring in the surface colour, then one in ink, as in the prototype.
    if (isConnectingFromHere())
    {
        g.setColour (p.surface);
        g.drawEllipse (disc.expanded (3.0f), 2.0f);
        g.setColour (sourceInk (p, source));
        g.drawEllipse (disc.expanded (5.0f), 2.0f);
    }
    else if (hasKeyboardFocus (false))
    {
        ScratchpadLookAndFeel::drawFocusRing (g, disc, disc.getWidth() * 0.5f - 2.5f, p.ink);
    }
}

void SourceHandle::mouseDown (const juce::MouseEvent&)
{
    dragStarted = false;
    ignoreClick = false;

    // Clicking an armed handle puts it down again.
    if (isEnabled() && model.getMode() == ModulationModel::Mode::armed && model.getConnectingSource() == source)
    {
        model.cancelConnecting();
        ignoreClick = true;
    }
}

void SourceHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (! isEnabled() || ignoreClick)
        return;

    if (! dragStarted && e.getDistanceFromDragStart() > 4)
    {
        dragStarted = true;
        model.beginDrag (source, centreOnScreen());
    }

    if (dragStarted)
        model.movePointer (e.getScreenPosition());
}

void SourceHandle::mouseUp (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    if (dragStarted)
    {
        dragStarted = false;
        auto* target = model.findTargetAt (e.getScreenPosition());

        if (target != nullptr && model.isValidTarget (*target, source))
            model.dropOn (*target, nullptr);
        else
            model.cancelConnecting();
    }
    else if (! ignoreClick && e.mouseWasClicked())
    {
        model.arm (source, centreOnScreen());
    }
}

void SourceHandle::activate()
{
    if (isConnectingFromHere())
        model.cancelConnecting();
    else
        model.chooseDestination (source, *this);
}

std::unique_ptr<juce::AccessibilityHandler> SourceHandle::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (
        *this, juce::AccessibilityRole::button,
        [this]
        {
            const auto name = info::sourceName (source);

            if (! isEnabled())
                return "Not available: " + name + " is not in the patch yet.";

            if (model.getMode() == ModulationModel::Mode::armed && isConnectingFromHere())
                return "Picked up. Click a knob to connect " + name + " to it, or press Enter to put it down.";

            return "Press Enter to choose what " + name + " moves. Or drag this handle onto a knob. Connections start at plus 50 percent.";
        },
        juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [this] { if (isEnabled()) activate(); }));
}

bool SourceHandle::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        activate();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && model.isConnecting())
    {
        model.cancelConnecting();
        return true;
    }

    return false;
}

//==============================================================================
LfoCard::LfoCard (plugin::ScratchpadProcessor& p, ModulationModel& m, int lfoIndex)
    : model (m),
      index (lfoIndex),
      handle (m, engine::lfoSource ((size_t) lfoIndex)),
      shape (p.apvts, params::lfo[(size_t) lfoIndex].shape, "LFO " + juce::String (lfoIndex + 1) + " shape", p.undoManager, &Palette::mod),
      retrigger (p.apvts, params::lfo[(size_t) lfoIndex].retrigger, "Retrigger",
                 "LFO " + juce::String (lfoIndex + 1) + " retrigger on note", p.undoManager, &Palette::modInk),
      sync (p.apvts, params::lfo[(size_t) lfoIndex].sync, "Sync",
            "LFO " + juce::String (lfoIndex + 1) + " tempo sync", p.undoManager, &Palette::modInk),
      rate (p.apvts, params::lfo[(size_t) lfoIndex].rate, "Rate", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " rate"),
      division (p.apvts, params::lfo[(size_t) lfoIndex].division, "Beat", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " note length"),
      fade (p.apvts, params::lfo[(size_t) lfoIndex].fade, "Fade-in", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " fade-in"),
      shapeWatcher (*p.apvts.getParameter (params::lfo[(size_t) lfoIndex].shape),
                    [this] (float v) { shapeIndex = juce::jlimit (0, dsp::numLfoShapes - 1, (int) std::lround (v)); repaint (vizWell); }),
      syncWatcher (*p.apvts.getParameter (params::lfo[(size_t) lfoIndex].sync), [this] (float) { refreshSync(); }),
      divisionWatcher (*p.apvts.getParameter (params::lfo[(size_t) lfoIndex].division), [this] (float) { refreshSync(); })
{
    const auto name = "LFO " + juce::String (index + 1);
    setTitle (name);
    makeControllable (p, "lfo" + juce::String (index + 1), false);
    makeRemovable ("Remove " + name, [&p, index = index] { p.removeModule (index == 0 ? plugin::Module::lfo1 : plugin::Module::lfo2); });

    shape.setGlyphs (shapeGlyphs());
    rate.setModulation (model, { engine::lfoRateDest ((size_t) index) });

    for (auto* c : std::initializer_list<juce::Component*> { &handle, &shape, &retrigger, &sync, &rate, &division, &fade, &vizRegion })
        addAndMakeVisible (c);

    vizRegion.setTitle (name + " waveform");
    vizRegion.setDescriber ([this]
                            {
                                auto& apvts = model.getProcessor().apvts;
                                const auto shapeName = params::lfoShapeNames()[juce::jlimit (0, dsp::numLfoShapes - 1, shapeIndex)];
                                const auto hz = a11y::spokenValue (apvts.getParameter (params::lfo[(size_t) index].rate)->getCurrentValueAsText());
                                const auto noteLength = apvts.getParameter (params::lfo[(size_t) index].division)->getCurrentValueAsText();

                                return shapeName + " wave, " + (synced ? "one cycle every " + noteLength + " note, following the tempo"
                                                                       : "repeating at " + hz) + ".";
                            });
    rate.setDisabledNote ("Not in use: tempo sync is on, so the note length sets the speed.");
    division.setDisabledNote ("Not in use: tempo sync is off, so the rate knob sets the speed.");
    setFocusOrder ({ &handle, &sync, &shape, &vizRegion, &retrigger, &rate, &division, &fade });

    model.addListener (this);
    applyColours();
    shapeWatcher.sendInitialUpdate();
    refreshSync();
}

LfoCard::~LfoCard()
{
    model.removeListener (this);
}

void LfoCard::applyColours()
{
    const auto ink = paletteOf (*this).modInk;
    rate.setFamilyColour (ink);
    division.setFamilyColour (ink);
    fade.setFamilyColour (ink);
}

void LfoCard::lookAndFeelChanged()
{
    applyColours();
}

// With Sync on, the note length sets the speed and the Rate knob steps back (it stays where it was, dimmed).
void LfoCard::refreshSync()
{
    // The parameters' own values: a listener runs before the cached atomics are updated.
    auto& apvts = model.getProcessor().apvts;
    const auto read = [&apvts] (const char* id)
    {
        auto* param = apvts.getParameter (id);
        return param->convertFrom0to1 (param->getValue());
    };

    const auto& ids = params::lfo[(size_t) index];
    synced = read (ids.sync) > 0.5f;
    divisionIndex = (int) std::lround (read (ids.division));

    rate.setEnabled (! synced);
    division.setEnabled (synced);
    shownHz = -1.0f;
    repaint (vizWell.expanded (4));
}

void LfoCard::resized()
{
    constexpr int left = 8, leftWidth = 116, top = 4;

    headerRow = { left, top, width - 2 * left, SourceHandle::diameter };
    handle.setBounds (left, top, SourceHandle::diameter, SourceHandle::diameter);
    placeRemoveButton (headerRow);
    sync.setBounds (88, top + 3, 62, 20);

    vizWell = { left, 34, leftWidth, 30 };
    vizRegion.setBounds (vizWell);
    shape.setBounds (left, 68, leftWidth, 22);
    retrigger.setBounds (left, 94, leftWidth, 18);

    rate.setBounds (130, 34, Knob::knobWidth, Knob::knobHeight);
    division.setBounds (182, 34, Knob::knobWidth, Knob::knobHeight);
    fade.setBounds (234, 34, Knob::knobWidth, Knob::knobHeight);
}

void LfoCard::liveUpdated()
{
    if (! isVisible())
        return;

    // A synced LFO's speed follows the host tempo, which can move while it plays.
    if (synced)
    {
        const float hz = params::lfoSyncRateHz (model.getProcessor().getBpm(), divisionIndex);

        if (std::abs (hz - shownHz) > 0.001f)
        {
            shownHz = hz;
            repaint (vizWell.expanded (4));
        }
    }

    const float phase = (float) model.liveLfoPhase (index);

    if (std::abs (phase - shownPhase) > 0.004f)
    {
        if (phase < shownPhase - 0.5f)
            secondCycle = ! secondCycle; // wrapped: the picture shows two cycles, so the dot moves on to the other one

        shownPhase = phase;
        repaint (vizWell.expanded (4));
    }
}

void LfoCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);

    g.setColour (p.ink);
    g.setFont (uiFont (13.0f, Weight::medium));
    g.drawText ("LFO " + juce::String (index + 1),
                headerRow.withTrimmedLeft (SourceHandle::diameter + 8).withTrimmedRight (76), juce::Justification::centredLeft, false);

    drawInset (g, vizWell.toFloat(), 8.0f, p, p.well);

    // Two cycles of the shape, with a dot that follows the phase. The dot sweeps both cycles, so it
    // needs to know which of the two it is on: count the wraps.
    const auto area = vizWell.toFloat().reduced (8.0f, 5.0f);
    const auto shapeValue = (dsp::LfoShape) shapeIndex;

    juce::Path wave;
    constexpr int points = 80;
    for (int i = 0; i <= points; ++i)
    {
        const double x = (double) i / points;
        const float y = area.getCentreY() - shapeAt (shapeValue, std::min (x, 0.9999)) * area.getHeight() * 0.5f;
        const float px = area.getX() + (float) x * area.getWidth();

        if (shapeValue == dsp::LfoShape::sampleHold && i > 0)
            wave.lineTo (px, wave.getCurrentPosition().y); // hold, then step

        if (i == 0)
            wave.startNewSubPath (px, y);
        else
            wave.lineTo (px, y);
    }

    g.setColour (p.modInk);
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (shownPhase >= 0.0f)
    {
        const double x = std::min (0.9999, ((double) shownPhase + (secondCycle ? 1.0 : 0.0)) * 0.5);
        const float dotY = area.getCentreY() - shapeAt (shapeValue, x) * area.getHeight() * 0.5f;
        const float dotX = area.getX() + (float) x * area.getWidth();
        g.setColour (p.ink);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ dotX, dotY }));
    }

    // The speed the tempo gives this LFO, while it is synced.
    if (synced)
    {
        const float hz = params::lfoSyncRateHz (model.getProcessor().getBpm(), divisionIndex);
        const auto label = juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz";
        const auto font = uiFont (11.0f);
        const auto tag = juce::Rectangle<float> (textWidth (font, label) + 8.0f, 13.0f)
                             .withRightX (vizWell.toFloat().getRight() - 3.0f).withY ((float) vizWell.getY() + 2.0f);
        g.setColour (p.well.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (p.ink2);
        g.setFont (font);
        g.drawText (label, tag, juce::Justification::centred, false);
    }
}

//==============================================================================
SourcesPanel::SourcesPanel (plugin::ScratchpadProcessor& p, ModulationModel& m)
    : processor (p)
{
    setTitle ("Other modulation sources");
    setExplainKey ("sources");

    const ModSource order[] = { ModSource::ampEnv, ModSource::filterEnv, ModSource::velocity,
                                ModSource::modWheel, ModSource::aftertouch, ModSource::key };

    for (size_t i = 0; i < chips.size(); ++i)
    {
        chips[i].handle = std::make_unique<SourceHandle> (m, order[i]);
        chips[i].name = sourceCaption (order[i]);
        addAndMakeVisible (*chips[i].handle);
    }

    refreshPresence();
}

void SourcesPanel::refreshPresence()
{
    for (auto& c : chips)
    {
        const auto source = c.handle->getSource();
        c.handle->setEnabled (processor.isModSourcePresent (source));
    }

    repaint();
}

void SourcesPanel::resized()
{
    constexpr int pitchX = 108, pitchY = 30, top = 24;

    captionRow = { pad, 6, width - 2 * pad, 14 };

    for (size_t i = 0; i < chips.size(); ++i)
    {
        const int col = (int) i % 2, row = (int) i / 2;
        auto& c = chips[i];
        c.area = { pad + col * pitchX, top + row * pitchY, pitchX - 4, SourceHandle::diameter };
        c.handle->setBounds (c.area.withWidth (SourceHandle::diameter + 6).withTrimmedLeft (0));
    }
}

void SourcesPanel::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawCaption (g, "Other sources", captionRow.toFloat(), p.ink2);

    g.setFont (uiFont (12.0f));

    for (const auto& c : chips)
    {
        const bool present = c.handle->isEnabled();
        g.setColour (present ? p.ink : p.mute.withMultipliedAlpha (0.6f));
        g.drawText (c.name, c.area.withTrimmedLeft (SourceHandle::diameter + 10), juce::Justification::centredLeft, true);
    }
}

//==============================================================================
ConnectionRow::ConnectionRow (ModulationModel& m, int slotIndex)
    : model (m), slot (slotIndex)
{
    setPaintingIsUnclipped (true);
    setExplicitFocusOrder (slotIndex + 1); // Tab visits slot 1 to 8, not the two columns row by row
    getProperties().set ("explainId", "mod" + juce::String (slotIndex + 1) + ".amount");
    refresh();
}

void ConnectionRow::refresh()
{
    const auto& c = model.getConnections()[(size_t) slot];
    amountPercent = c.amount * 100.0f;

    setTitle ("Modulation slot " + juce::String (slot + 1) + (c.active() ? ": " + info::sourceName (c.source) + " to " + destinationLongName (c.dest)
                                                                       : juce::String (": free")));
    setMouseCursor (c.active() ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);

    // Only an occupied row is somewhere to go with the keyboard; an empty one is text. Becoming one or the other
    // changes what a screen reader is looking at, so it gets a new handler.
    setWantsKeyboardFocus (c.active());

    if (c.active() != wasActive)
    {
        wasActive = c.active();
        invalidateAccessibilityHandler();
    }
    else if (auto* handler = getAccessibilityHandler())
    {
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    }

    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> ConnectionRow::createAccessibilityHandler()
{
    if (! model.getConnections()[(size_t) slot].active())
        return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::staticText,
                                                         [] { return juce::String ("Empty. Choose a source handle and then what it should move, to fill it."); });

    struct Amount final : juce::AccessibilityValueInterface
    {
        explicit Amount (ConnectionRow& r) : row (r) {}

        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return row.model.getProcessor().getModConnections()[(size_t) row.slot].amount * 100.0; }

        void setValue (double percent) override
        {
            auto& processor = row.model.getProcessor();
            processor.beginModAmountEdit (row.slot);
            processor.setModAmount (row.slot, (float) juce::jlimit (-100.0, 100.0, percent));
            processor.endModAmountEdit (row.slot);
        }

        juce::String getCurrentValueAsString() const override
        {
            return a11y::spokenValue (info::amountText (row.model.getProcessor().getModConnections()[(size_t) row.slot].amount));
        }

        void setValueAsString (const juce::String& text) override { setValue (text.getDoubleValue()); }
        AccessibleValueRange getRange() const override { return { { -100.0, 100.0 }, 1.0 }; }

        ConnectionRow& row;
    };

    return std::make_unique<a11y::DescribedHandler> (
        *this, juce::AccessibilityRole::slider,
        [this]
        {
            const auto& c = model.getConnections()[(size_t) slot];
            return "How far " + info::sourceName (c.source) + " moves " + destinationLongName (c.dest)
                   + ". Left and right arrows change it, Shift for bigger steps. Delete removes the connection.";
        },
        juce::AccessibilityActions {},
        juce::AccessibilityHandler::Interfaces { std::make_unique<Amount> (*this) });
}

void ConnectionRow::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
    const auto& c = model.getConnections()[(size_t) slot];

    if (! c.active())
    {
        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), 8.0f);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (p.dash.withAlpha (0.55f));
        g.fillPath (dashed);
        return;
    }

    drawInset (g, bounds, 8.0f, p, p.well);

    // The amount as a bar growing from the middle, in the source's colour.
    const auto inside = bounds.reduced (3.0f, 3.0f);
    const float mid = inside.getCentreX();
    const float end = mid + (float) c.amount * inside.getWidth() * 0.5f;
    g.setColour (sourceFill (p, c.source).withAlpha (0.28f));
    g.fillRoundedRectangle ({ std::min (mid, end), inside.getY(), std::max (std::abs (end - mid), 2.0f), inside.getHeight() }, 5.0f);

    // Source tag.
    auto row = bounds.reduced (5.0f, 0.0f);
    const auto tag = row.removeFromLeft (26.0f).reduced (0.0f, 3.0f);
    g.setColour (sourceFill (p, c.source));
    g.fillRoundedRectangle (tag, tag.getHeight() * 0.5f);
    g.setColour (p.onAccent);
    g.setFont (uiFont (11.0f, Weight::medium));
    g.drawText (info::sourceLabel (c.source), tag, juce::Justification::centred, false);

    // Cross, amount, destination.
    const auto cross = removeArea().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
    row.removeFromRight (20.0f);
    g.setColour (overRemove ? p.ink : p.mute);
    g.drawLine (cross.getX() + 3.5f, cross.getY() + 3.5f, cross.getRight() - 3.5f, cross.getBottom() - 3.5f, 1.3f);
    g.drawLine (cross.getRight() - 3.5f, cross.getY() + 3.5f, cross.getX() + 3.5f, cross.getBottom() - 3.5f, 1.3f);

    g.setColour (p.ink);
    g.setFont (uiFont (11.0f));
    const auto amount = row.removeFromRight (34.0f);
    g.drawText (info::amountText (c.amount), amount, juce::Justification::centredRight, false);
    g.setColour (p.ink2);
    g.drawText (info::destinationName (c.dest), row.withTrimmedLeft (5.0f), juce::Justification::centredLeft, true);

    // Inside the row's own edges, so the next row (which paints after this one) does not cover the ring.
    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds.reduced (3.5f), 5.5f, p.ink);
}

void ConnectionRow::mouseMove (const juce::MouseEvent& e)
{
    const bool over = model.getConnections()[(size_t) slot].active() && removeArea().contains (e.getPosition());

    if (over != overRemove)
    {
        overRemove = over;
        repaint();
    }
}

void ConnectionRow::mouseExit (const juce::MouseEvent&)
{
    if (overRemove)
    {
        overRemove = false;
        repaint();
    }
}

void ConnectionRow::mouseDown (const juce::MouseEvent& e)
{
    lastDragX = e.getPosition().x;

    if (! model.getConnections()[(size_t) slot].active())
        return;

    grabKeyboardFocus();

    if (removeArea().contains (e.getPosition()))
    {
        model.getProcessor().disconnectMod (slot);
        return;
    }

    if (e.mods.isPopupMenu())
    {
        showRemoveMenu();
        return;
    }

    dragging = true;
    amountPercent = model.getConnections()[(size_t) slot].amount * 100.0f;
    model.getProcessor().beginModAmountEdit (slot);
}

void ConnectionRow::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    // 100 px sweeps the whole range; Shift is four times finer.
    const int dx = e.getPosition().x - lastDragX;
    lastDragX = e.getPosition().x;
    amountPercent = juce::jlimit (-100.0f, 100.0f, amountPercent + (float) dx * (e.mods.isShiftDown() ? 0.5f : 2.0f));
    model.getProcessor().setModAmount (slot, amountPercent);
}

void ConnectionRow::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        model.getProcessor().endModAmountEdit (slot);
    }
}

void ConnectionRow::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! model.getConnections()[(size_t) slot].active() || removeArea().contains (e.getPosition()))
        return;

    auto& processor = model.getProcessor();
    processor.beginModAmountEdit (slot);
    processor.setModAmount (slot, params::modAmountDefault);
    processor.endModAmountEdit (slot);
}

void ConnectionRow::showRemoveMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Remove " + info::connectionText (model.getProcessor().getModConnections()[(size_t) slot]));
    juce::Component::SafePointer<ConnectionRow> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->remove();
                        });
}

void ConnectionRow::remove()
{
    // The row stops being somewhere to go with the keyboard, so move on first.
    if (hasKeyboardFocus (false))
        moveKeyboardFocusToSibling (true);

    model.getProcessor().disconnectMod (slot);
}

void ConnectionRow::nudge (float percent)
{
    auto& processor = model.getProcessor();
    processor.beginModAmountEdit (slot);
    processor.setModAmount (slot, processor.getModConnections()[(size_t) slot].amount * 100.0f + percent);
    processor.endModAmountEdit (slot);
}

bool ConnectionRow::keyPressed (const juce::KeyPress& key)
{
    if (! model.getProcessor().getModConnections()[(size_t) slot].active())
        return false;

    const auto mods = key.getModifiers();

    if (mods.isCommandDown() || mods.isAltDown() || mods.isCtrlDown())
        return false;

    const int code = key.getKeyCode();
    const float step = mods.isShiftDown() ? 10.0f : 1.0f;

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)
    {
        nudge (-step);
        return true;
    }

    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)
    {
        nudge (step);
        return true;
    }

    if (code == juce::KeyPress::pageDownKey || code == juce::KeyPress::pageUpKey)
    {
        nudge (code == juce::KeyPress::pageUpKey ? 10.0f : -10.0f);
        return true;
    }

    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::returnKey)
    {
        auto& processor = model.getProcessor();
        processor.beginModAmountEdit (slot);
        processor.setModAmount (slot, params::modAmountDefault);
        processor.endModAmountEdit (slot);
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        remove();
        return true;
    }

    if (code == juce::KeyPress::F10Key && mods.isShiftDown())
    {
        showRemoveMenu();
        return true;
    }

    return false;
}

//==============================================================================
ConnectionsPanel::ConnectionsPanel (ModulationModel& m)
    : model (m)
{
    setTitle ("Modulation connections");
    setExplainKey ("connections");

    for (size_t i = 0; i < rows.size(); ++i)
    {
        rows[i] = std::make_unique<ConnectionRow> (m, (int) i);
        addAndMakeVisible (*rows[i]);
    }

    model.addListener (this);
}

ConnectionsPanel::~ConnectionsPanel()
{
    model.removeListener (this);
}

void ConnectionsPanel::modulationChanged()
{
    for (auto& r : rows)
        r->refresh();

    repaint (captionRow);
}

void ConnectionsPanel::resized()
{
    constexpr int top = 24, gap = 4;
    const int rowWidth = (getWidth() - 2 * pad - gap) / 2;

    captionRow = { pad, 6, getWidth() - 2 * pad, 14 };

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int col = (int) i / 4, row = (int) i % 4;
        rows[i]->setBounds (pad + col * (rowWidth + gap), top + row * ConnectionRow::height, rowWidth, ConnectionRow::height);
    }
}

void ConnectionsPanel::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawCaption (g, "Connections", captionRow.toFloat(), p.ink2);
    g.setColour (p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String (model.countConnections()) + " of " + juce::String (engine::numModSlots),
                captionRow, juce::Justification::centredRight, false);
}

} // namespace scratchpad::ui
