#include "ui/ModulationStrip.h"

#include "plugin/Parameters.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

using engine::ModSource;
namespace info = plugin::modulation;

namespace
{
    /** What the shape well names (and More's segments say): the parameter's words, "S&H" for sample and hold. */
    const juce::StringArray& shapeWords()
    {
        static const juce::StringArray words { "Sine", "Triangle", "Saw up", "Saw down", "Square", "S&H" };
        return words;
    }

    /** The shape strip's segments, short enough for 29 px each. */
    const juce::StringArray& shapeStripWords()
    {
        static const juce::StringArray words { "Sine", "Tri", juce::String (juce::CharPointer_UTF8 ("Saw\xe2\x86\x91")),
                                               juce::String (juce::CharPointer_UTF8 ("Saw\xe2\x86\x93")), "Sq", "S&H" };
        return words;
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

    // "KEY" is as wide as the disc allows.
    const auto label = info::sourceLabel (source);
    auto font = uiFont (11.0f, Weight::medium);
    if (textWidth (font, label) > disc.getWidth() - 3.0f)
        font = uiFont (10.0f, Weight::medium);

    g.setColour (p.onAccent);
    g.setFont (font);
    g.drawText (label, disc, juce::Justification::centred, false);

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
TogglePill::TogglePill (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& labelText,
                        const juce::String& accessibleName, juce::UndoManager& undoToUse)
    : juce::Button (labelText), label (labelText), undo (undoToUse)
{
    auto* param = state.getParameter (paramID);
    jassert (param != nullptr);

    setTitle (accessibleName);

    if (const auto* entry = content::Content::get().param (paramID))
        setDescription (entry->what);

    getProperties().set ("explainId", paramID);
    setPaintingIsUnclipped (true);
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::ButtonParameterAttachment> (*param, *this, &undo);

    // Runs after the attachment has written the value.
    onClick = [this] { undo.setCurrentTransactionName (getTitle() + (getToggleState() ? " on" : " off")); };
}

void TogglePill::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat();
    const bool on = getToggleState();

    if (on)
    {
        g.setColour (p.mod);
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    }
    else
    {
        g.setColour (p.track);
        g.drawRoundedRectangle (bounds.reduced (0.5f), bounds.getHeight() * 0.5f - 0.5f, 1.0f);
    }

    g.setColour (on ? p.onAccent : highlighted ? p.ink : p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText (label, bounds, juce::Justification::centred, false);

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds, bounds.getHeight() * 0.5f - 2.5f, p.ink);
}

//==============================================================================
/** The shape well: a button (it opens More, where the shape is chosen) that the card paints. */
class LfoCard::ShapeWell final : public juce::Button
{
public:
    explicit ShapeWell (LfoCard& card, const juce::String& explainId)
        : juce::Button ("Shape"), owner (card)
    {
        getProperties().set ("explainId", explainId);
        setClickingTogglesState (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        owner.paintWell (g, getLocalBounds().toFloat());

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().reduced (1.0f), 8.0f, paletteOf (*this).ink);
    }

private:
    LfoCard& owner;
};

LfoCard::LfoCard (plugin::ScratchpadProcessor& p, ModulationModel& m, int lfoIndex)
    : model (m),
      index (lfoIndex),
      handle (m, engine::lfoSource ((size_t) lfoIndex)),
      sync (p.apvts, params::lfo[(size_t) lfoIndex].sync, "Sync", "LFO " + juce::String (lfoIndex + 1) + " tempo sync", p.undoManager),
      rate (p.apvts, params::lfo[(size_t) lfoIndex].rate, "Rate", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " rate", layout::knobs::lfo),
      division (p.apvts, params::lfo[(size_t) lfoIndex].division, "Beat", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " note length", layout::knobs::lfo),
      fade (p.apvts, params::lfo[(size_t) lfoIndex].fade, "Fade", p.undoManager, "LFO " + juce::String (lfoIndex + 1) + " fade-in", layout::knobs::lfo),
      shapeStrip (p.apvts, params::lfo[(size_t) lfoIndex].shape, "LFO " + juce::String (lfoIndex + 1) + " shape", p.undoManager, &Palette::mod),
      retrigger (p.apvts, params::lfo[(size_t) lfoIndex].retrigger, "Retrig", "LFO " + juce::String (lfoIndex + 1) + " retrigger on note", p.undoManager),
      shapeWatcher (*p.apvts.getParameter (params::lfo[(size_t) lfoIndex].shape),
                    [this] (float v)
                    {
                        shapeIndex = juce::jlimit (0, dsp::numLfoShapes - 1, (int) std::lround (v));
                        describeWell();
                    }),
      syncWatcher (*p.apvts.getParameter (params::lfo[(size_t) lfoIndex].sync), [this] (float) { refreshSync(); })
{
    const auto name = "LFO " + juce::String (index + 1);
    const auto& ids = params::lfo[(size_t) index];
    setTitle (name);
    makeControllable (p, "lfo" + juce::String (index + 1), false);
    makeMore (name, &Palette::mod,
              [this, name] (MorePopover& popover)
              {
                  auto& processor = model.getProcessor();
                  const auto& lfo = params::lfo[(size_t) index];
                  // The shape and retrigger are on the card while it is full; More only has them while it is compact.
                  if (! isFull())
                  {
                      popover.addSegments (processor, lfo.shape, "Shape", name + " shape", 3, shapeWords());
                      popover.addSwitch (processor, lfo.retrigger, "Retrigger on each note", name + " retrigger on note");
                  }
              },
              "Remove " + name, "Remove " + name,
              [&p, index = index] { p.removeModule (index == 0 ? plugin::Module::lfo1 : plugin::Module::lfo2); });

    rate.setModulation (model, { engine::lfoRateDest ((size_t) index) });

    well = std::make_unique<ShapeWell> (*this, ids.shape);
    well->onClick = [this] { toggleMore(); };

    shapeStrip.setColumns (3);
    shapeStrip.setDisplayLabels (shapeStripWords());

    for (auto* c : std::initializer_list<juce::Component*> { &handle, &sync, well.get(), &rate, &division, &fade, &shapeStrip, &retrigger })
        addAndMakeVisible (c);

    // Left to right: the well, the knobs, then the shape strip and Retrig (which only a full card shows).
    setFocusOrder ({ &handle, &sync, well.get(), &rate, &division, &fade, &shapeStrip, &retrigger });

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

// With Sync on, the note length takes the Rate knob's place and caption: one knob for one idea.
void LfoCard::refreshSync()
{
    // The parameters' own values: a listener runs before the cached atomics are updated.
    synced = model.getProcessor().apvts.getParameter (params::lfo[(size_t) index].sync)->getValue() > 0.5f;
    rate.setVisible (! synced);
    division.setVisible (synced);
    describeWell();
}

void LfoCard::describeWell()
{
    if (well == nullptr)
        return;

    auto& apvts = model.getProcessor().apvts;
    const auto shapeName = shapeWords()[shapeIndex];
    const auto hz = a11y::spokenValue (apvts.getParameter (params::lfo[(size_t) index].rate)->getCurrentValueAsText());
    const auto noteLength = apvts.getParameter (params::lfo[(size_t) index].division)->getCurrentValueAsText();

    well->setTitle (getTitle() + " shape, " + shapeName + (isFull() ? "" : ". Choose a shape"));
    well->setDescription (params::lfoShapeNames()[shapeIndex] + " wave, "
                          + (synced ? "one cycle every " + noteLength + " note, following the tempo" : "repeating at " + hz) + ".");
    well->repaint();
}

void LfoCard::resized()
{
    namespace pad = layout::padding;
    constexpr int wellHeight = 40, rowGap = 6, bodyGap = 6;
    constexpr int stripWidth = 96, stripHeight = 40, retrigWidth = 42, retrigHeight = 24;

    const bool full = isFull();

    headerRow = { pad::lfo + 1, pad::lfo, getWidth() - 2 * (pad::lfo + 1), layout::header::lfoHeight };
    handle.setBounds (headerRow.withWidth (SourceHandle::diameter));
    placeHeader (headerRow, layout::header::handle);

    const int buttonsWidth = 2 * (layout::header::buttonWidth + layout::header::buttonGap);
    sync.setBounds (headerRow.getRight() - buttonsWidth - TogglePill::width, headerRow.getCentreY() - TogglePill::height / 2,
                    TogglePill::width, TogglePill::height);

    // The well takes whatever width and height the box has over the compact design; the knobs, the strip and Retrig
    // keep their size and stay against the bottom and the right edge.
    const int extra = juce::jmax (0, getHeight() - layout::lfo1.h);
    const int bodyY = headerRow.getBottom() + rowGap;
    const int knobsWidth = 2 * layout::knobs::lfo.cell + bodyGap;
    const int stripArea = full ? stripWidth + retrigWidth + 2 * bodyGap : 0;
    wellBounds = { headerRow.getX(), bodyY, headerRow.getWidth() - knobsWidth - bodyGap - stripArea, wellHeight + extra };
    well->setBounds (wellBounds);

    const int knobX = wellBounds.getRight() + bodyGap;
    rate.setBoundsAt (knobX, bodyY + extra);
    division.setBoundsAt (knobX, bodyY + extra);
    fade.setBoundsAt (knobX + layout::knobs::lfo.cell + bodyGap, bodyY + extra);

    // Full: the shape picker is on the card, so the well only shows the wave and is not a way to More.
    shapeStrip.setVisible (full);
    retrigger.setVisible (full);
    well->setWantsKeyboardFocus (! full);
    well->setInterceptsMouseClicks (! full, false);
    well->setAccessible (! full);

    if (full)
    {
        const int stripX = headerRow.getRight() - retrigWidth - bodyGap - stripWidth;
        shapeStrip.setBounds (stripX, bodyY + extra, stripWidth, stripHeight);
        retrigger.setBounds (headerRow.getRight() - retrigWidth, shapeStrip.getBounds().getCentreY() - retrigHeight / 2, retrigWidth, retrigHeight);
    }

    describeWell();
}

void LfoCard::liveUpdated()
{
    if (! isVisible())
        return;

    const float phase = (float) model.liveLfoPhase (index);

    if (std::abs (phase - shownPhase) > 0.004f)
    {
        if (phase < shownPhase - 0.5f)
            secondCycle = ! secondCycle; // wrapped: the picture shows two cycles, so the dot moves on to the other one

        shownPhase = phase;
        well->repaint();
    }
}

// Two cycles of the shape, with a dot that follows the phase. The dot sweeps both cycles, so it needs to know which
// of the two it is on: count the wraps.
void LfoCard::paintWell (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    const auto& p = paletteOf (*this);
    drawInset (g, bounds, 9.0f, p, p.well);

    const auto area = bounds.reduced (4.0f, 8.0f);
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
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));

    if (shownPhase >= 0.0f)
    {
        const double x = std::min (0.9999, ((double) shownPhase + (secondCycle ? 1.0 : 0.0)) * 0.5);
        const float dotY = area.getCentreY() - shapeAt (shapeValue, x) * area.getHeight() * 0.5f;
        const float dotX = area.getX() + (float) x * area.getWidth();
        g.setColour (p.ink);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ dotX, dotY }));
    }

    if (isFull())
        return;

    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    g.drawText (shapeWords()[shapeIndex], bounds.withTrimmedLeft (6.0f).withTrimmedTop (3.0f).withHeight (15.0f),
                juce::Justification::centredLeft, true);
}

void LfoCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);
    drawHeader (g, paletteOf (*this), headerRow.toFloat(), paletteOf (*this).mod, nullptr, getTitle(), layout::header::handle);
}

//==============================================================================
PlayingCard::PlayingCard (plugin::ScratchpadProcessor&, ModulationModel& m)
{
    setTitle ("Playing sources");
    setExplainKey ("sources");

    const ModSource order[] = { ModSource::velocity, ModSource::modWheel, ModSource::aftertouch, ModSource::key };
    const char* const names[] = { "Velocity", "Mod wheel", "Aftertouch", "Key" };

    for (size_t i = 0; i < chips.size(); ++i)
    {
        chips[i].handle = std::make_unique<SourceHandle> (m, order[i]);
        chips[i].name = names[i];
        addAndMakeVisible (*chips[i].handle);
    }
}

void PlayingCard::resized()
{
    namespace pad = layout::padding;
    constexpr int captionHeight = 14, gridGap = 6, labelGap = 5;

    const int inner = getWidth() - 2 * pad::playing - 2;
    const int columnWidth = (inner - gridGap) / 2;
    const int top = pad::playing + captionHeight + gridGap;

    captionRow = { pad::playing + 2, pad::playing, inner, captionHeight };

    for (size_t i = 0; i < chips.size(); ++i)
    {
        const int x = captionRow.getX() + ((int) i % 2) * (columnWidth + gridGap);
        const int y = top + ((int) i / 2) * (SourceHandle::diameter + gridGap);
        chips[i].handle->setBounds (x, y, SourceHandle::diameter, SourceHandle::diameter);
        chips[i].labelArea = { x + SourceHandle::diameter + labelGap, y, columnWidth - SourceHandle::diameter - labelGap, SourceHandle::diameter };
    }
}

void PlayingCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawCaption (g, "Playing", captionRow.toFloat(), p.ink2);

    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));

    for (const auto& c : chips)
        g.drawText (c.name, c.labelArea, juce::Justification::centredLeft, false);
}

//==============================================================================
ConnectionRow::ConnectionRow (ModulationModel& m, int slotIndex)
    : model (m), slot (slotIndex)
{
    setPaintingIsUnclipped (true);
    setExplicitFocusOrder (slotIndex + 1); // Tab visits slot 1 to 8
    getProperties().set ("explainId", "mod" + juce::String (slotIndex + 1) + ".amount");
    refresh();
}

bool ConnectionRow::isFilled() const
{
    return model.getConnections()[(size_t) slot].active();
}

void ConnectionRow::setShowsEmpty (bool shouldShow)
{
    if (showsEmpty != shouldShow)
    {
        showsEmpty = shouldShow;
        repaint();
    }
}

void ConnectionRow::focusGained (FocusChangeType)
{
    repaint();

    if (onFocused)
        onFocused (*this);
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
    const auto bounds = getLocalBounds().toFloat();
    const auto& c = model.getConnections()[(size_t) slot];

    if (! c.active())
    {
        if (showsEmpty)
        {
            juce::Path outline;
            outline.addRoundedRectangle (bounds.reduced (0.75f), 6.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (p.dash);
            g.fillPath (dashed);

            g.setColour (p.mute);
            g.setFont (uiFont (11.0f));
            g.drawText ("Empty slot", bounds.withTrimmedLeft (8.0f), juce::Justification::centredLeft, true);
        }

        return;
    }

    constexpr float chipWidth = 22.0f, chipHeight = 16.0f, destWidth = 76.0f, amountWidth = 30.0f, gap = 7.0f, barHeight = 6.0f;

    auto row = bounds;

    const auto chip = row.removeFromLeft (chipWidth).withSizeKeepingCentre (chipWidth, chipHeight);
    g.setColour (sourceFill (p, c.source));
    g.fillRoundedRectangle (chip, chipHeight * 0.5f);
    g.setColour (p.onAccent);
    g.setFont (uiFont (11.0f));
    g.drawText (info::sourceLabel (c.source), chip, juce::Justification::centred, false);

    row.removeFromLeft (gap);
    g.setColour (p.ink);
    g.setFont (uiFont (12.0f));
    g.drawText (info::destinationName (c.dest), row.removeFromLeft (destWidth), juce::Justification::centredLeft, true);
    row.removeFromLeft (gap);

    // The cross, then the amount, then (what is left) the bar, which grows from its middle in the source's colour.
    const auto cross = removeArea().toFloat();
    row.removeFromRight ((float) removeWidth + gap);

    const auto amount = row.removeFromRight (amountWidth);
    row.removeFromRight (gap);
    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    g.drawText (info::amountText (c.amount), amount, juce::Justification::centredRight, false);

    const auto bar = row.withSizeKeepingCentre (row.getWidth(), barHeight);
    drawInset (g, bar, barHeight * 0.5f, p, p.well);

    const float mid = bar.getCentreX();
    const float end = mid + (float) c.amount * bar.getWidth() * 0.5f;

    if (std::abs (end - mid) >= 1.0f)
    {
        g.setColour (sourceInk (p, c.source));
        g.fillRoundedRectangle ({ std::min (mid, end), bar.getY(), std::abs (end - mid), barHeight }, barHeight * 0.5f);
    }

    g.setColour (p.mute);
    g.fillRect (juce::Rectangle<float> (1.0f, 10.0f).withCentre ({ mid, bar.getCentreY() }));

    const auto mark = cross.withSizeKeepingCentre (8.0f, 8.0f);
    g.setColour (overRemove ? p.ink : p.mute);
    g.drawLine (mark.getX(), mark.getY(), mark.getRight(), mark.getBottom(), 1.3f);
    g.drawLine (mark.getRight(), mark.getY(), mark.getX(), mark.getBottom(), 1.3f);

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds.reduced (1.0f), 6.0f, p.ink);
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
    showStyledMenu (menu, juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int result)
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
/** Clips the rows to the window of the list that is in view. */
class ConnectionsPanel::List final : public juce::Component
{
public:
    List() { setInterceptsMouseClicks (false, true); }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return a11y::makeIgnoredHandler (*this);
    }
};

namespace
{
    constexpr int rowPitch = ConnectionRow::height + 2, columnGap = 12;
}

ConnectionsPanel::ConnectionsPanel (ModulationModel& m)
    : model (m), list (std::make_unique<List>())
{
    setTitle ("Modulation connections");
    setExplainKey ("connections");

    addAndMakeVisible (*list);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        rows[i] = std::make_unique<ConnectionRow> (m, (int) i);
        rows[i]->onFocused = [this] (ConnectionRow& row) { reveal (row); };
        list->addChildComponent (*rows[i]);
    }

    layoutRows();
    model.addListener (this);
}

ConnectionsPanel::~ConnectionsPanel()
{
    model.removeListener (this);
}

int ConnectionsPanel::filledCount() const
{
    return (int) std::count_if (rows.begin(), rows.end(), [] (const auto& r) { return r->isFilled(); });
}

void ConnectionsPanel::modulationChanged()
{
    for (auto& r : rows)
        r->refresh();

    layoutRows();
    repaint();
}

// What the list holds, in the order it shows it: the filled slots in slot order, then (with columns) the empty ones.
int ConnectionsPanel::positionCount() const
{
    return columns > 1 ? engine::numModSlots : filledCount();
}

int ConnectionsPanel::maxFirstRow() const
{
    return juce::jmax (0, positionCount() - rowsPerColumn * columns);
}

// The list fills down a column and then the next one (the order the accessibility tree, and Tab, visit it in).
// `firstRow` is the position at the top of the first column; scrolling moves the whole sequence on by one. A row
// outside the window stays a (clipped) child, just off the list's edge, so Tab can still land on it and bring it in.
void ConnectionsPanel::layoutRows()
{
    std::array<ConnectionRow*, engine::numModSlots> order {};
    int count = 0;

    for (auto& r : rows)
        if (r->isFilled())
            order[(size_t) count++] = r.get();

    if (columns > 1)
        for (auto& r : rows)
            if (! r->isFilled())
                order[(size_t) count++] = r.get();

    for (auto& r : rows)
        if (std::find (order.begin(), order.begin() + count, r.get()) == order.begin() + count)
            r->setVisible (false); // an empty slot in the short list

    firstRow = juce::jlimit (0, maxFirstRow(), firstRow);

    const int capacity = rowsPerColumn * columns;
    const int columnWidth = (list->getWidth() - columnGap * (columns - 1)) / columns;

    for (int position = 0; position < count; ++position)
    {
        auto& r = *order[(size_t) position];
        const int slotInView = position - firstRow;

        r.setShowsEmpty (columns > 1);
        r.setExplicitFocusOrder (position + 1);
        r.setVisible (true);

        if (slotInView >= 0 && slotInView < capacity)
            r.setBounds ((slotInView / rowsPerColumn) * (columnWidth + columnGap), (slotInView % rowsPerColumn) * rowPitch,
                         columnWidth, ConnectionRow::height);
        else
            r.setBounds (slotInView < 0 ? -columnWidth - columnGap : list->getWidth() + columnGap, 0, columnWidth, ConnectionRow::height);
    }

    // Children in the order they read, so a screen reader goes down the first column and then the next.
    bool inOrder = true;

    for (int position = 1; position < count && inOrder; ++position)
        inOrder = list->getIndexOfChildComponent (order[(size_t) position - 1]) < list->getIndexOfChildComponent (order[(size_t) position]);

    if (! inOrder)
        for (int position = 0; position < count; ++position)
            order[(size_t) position]->toFront (false);
}

void ConnectionsPanel::scrollTo (int row)
{
    const int clamped = juce::jlimit (0, maxFirstRow(), row);

    if (clamped != firstRow)
    {
        firstRow = clamped;
        layoutRows();
    }
}

void ConnectionsPanel::reveal (ConnectionRow& row)
{
    int position = 0;

    for (auto& r : rows)
    {
        if (r.get() == &row)
            break;

        position += r->isFilled() ? 1 : 0;
    }

    if (position < firstRow)
        scrollTo (position);
    else if (position >= firstRow + rowsPerColumn * columns)
        scrollTo (position - rowsPerColumn * columns + 1);
}

void ConnectionsPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (maxFirstRow() == 0)
        return;

    // A notch is about 0.1 of a row's worth of delta on most systems; smooth-scrolling pads send many small ones.
    wheelRemainder += (wheel.isReversed ? -1.0f : 1.0f) * wheel.deltaY * 10.0f;
    const int steps = (int) wheelRemainder;

    if (steps != 0)
    {
        wheelRemainder -= (float) steps;
        scrollTo (firstRow - steps);
    }
}

void ConnectionsPanel::resized()
{
    namespace pad = layout::padding;
    constexpr int captionHeight = 14, captionGap = 1, bottomPadding = 6;

    const int inner = getWidth() - 2 * (pad::playing + 2);
    captionRow = { pad::playing + 2, pad::playing - 1, inner, captionHeight };

    // Rows are the compact design's three at the least, and as many more as the box is tall enough for. At
    // `layout::full::connectionsTallHeight` that is four, so two columns hold all eight slots.
    columns = getWidth() >= layout::full::connectionsColumnsWidth ? 2 : 1;
    const int top = captionRow.getBottom() + captionGap + 2;
    const int available = getHeight() - top - bottomPadding;
    rowsPerColumn = juce::jlimit (designRows, (engine::numModSlots + columns - 1) / columns,
                                  (available - ConnectionRow::height) / rowPitch + 1);

    listArea = { captionRow.getX(), top, inner, (rowsPerColumn - 1) * rowPitch + ConnectionRow::height };
    list->setBounds (listArea);
    layoutRows();
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

    // With the list in columns the empty slots are drawn as placeholders (and say so); the hint is for the short list.
    if (model.countConnections() == 0 && columns == 1)
    {
        g.setFont (uiFont (12.0f));
        g.drawText ("Drag a coloured handle onto any knob.", listArea.withHeight (ConnectionRow::height), juce::Justification::centredLeft, true);
    }
}

} // namespace scratchpad::ui
