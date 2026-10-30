#include "ui/ModOverlay.h"

#include "ui/LookAndFeel.h"
#include "ui/ModArcs.h"

#include <cmath>

namespace scratchpad::ui
{

using engine::ModSource;
namespace info = plugin::modulation;

namespace
{
    constexpr float ringGrab = 3.0f; // how far either side of an arc still counts as on it
}

ModOverlay::ModOverlay (ModulationModel& m)
    : model (m)
{
    setInterceptsMouseClicks (true, false);
    setPaintingIsUnclipped (true);
    setTitle ("Modulation overlay");
    setAccessible (false);
    model.addListener (this);
}

ModOverlay::~ModOverlay()
{
    model.removeListener (this);
}

ModOverlay::RingHit ModOverlay::findRing (juce::Point<int> overlayPoint) const
{
    RingHit best;
    float bestGap = ringGrab + 0.001f;

    for (auto* t : model.getTargets())
    {
        auto& c = t->getComponent();

        if (! c.isShowing())
            continue;

        const auto local = c.getLocalPoint (this, overlayPoint).toFloat();
        const float distance = local.getDistanceFrom (t->getRingCentre());

        if (distance > modArcRadius (8) + ringGrab || distance < ModTarget::firstArcRadius - ringGrab)
            continue;

        const auto arcs = t->getArcs();

        for (size_t i = 0; i < arcs.size(); ++i)
        {
            const float gap = std::abs (distance - modArcRadius (i));

            if (arcs[i].slot >= 0 && gap < bestGap)
            {
                bestGap = gap;
                best = { t, arcs[i], i };
            }
        }
    }

    return best;
}

ModTarget* ModOverlay::findValidTarget (juce::Point<int> overlayPoint) const
{
    auto* t = model.findTargetAt (localPointToGlobal (overlayPoint));
    return t != nullptr && model.isValidTarget (*t, model.getConnectingSource()) ? t : nullptr;
}

bool ModOverlay::hitTest (int x, int y)
{
    if (editingSlot >= 0)
        return true;

    switch (model.getMode())
    {
        case ModulationModel::Mode::armed:    return findValidTarget ({ x, y }) != nullptr;
        case ModulationModel::Mode::dragging: return false; // the handle has the mouse
        case ModulationModel::Mode::idle:     break;
    }

    return findRing ({ x, y }).target != nullptr;
}

void ModOverlay::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto source = model.getConnectingSource();

    if (model.isConnecting())
    {
        // Light the knobs this source can go to.
        for (auto* t : model.getTargets())
        {
            auto& c = t->getComponent();

            if (! c.isShowing() || ! model.isValidTarget (*t, source))
                continue;

            const auto centre = getLocalPoint (&c, t->getRingCentre());
            g.setColour (p.accentInk);
            g.drawEllipse (juce::Rectangle<float> (1.0f, 1.0f).withCentre (centre).expanded (t->getDiscRadius() + 6.0f), 3.0f);
        }
    }

    if (model.getMode() == ModulationModel::Mode::dragging)
    {
        const auto start = getLocalPoint (nullptr, model.getHandleCentre()).toFloat();
        auto end = getLocalPoint (nullptr, model.getPointer()).toFloat();

        // Snap to the knob under the pointer, so it is clear where a drop will go.
        if (auto* t = findValidTarget (end.toInt()))
            end = getLocalPoint (&t->getComponent(), t->getRingCentre());

        const float reach = juce::jlimit (40.0f, 140.0f, start.getDistanceFrom (end) * 0.5f);
        juce::Path cable;
        cable.startNewSubPath (start);
        cable.cubicTo ({ start.x, start.y - reach }, { end.x, end.y + reach }, end);

        const auto colour = sourceFill (p, source);
        juce::Path dashed;
        const float dashes[] = { 7.0f, 4.0f };
        juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createDashedStroke (dashed, cable, dashes, 2);
        g.setColour (sourceInk (p, source)); // the line is a graphic that must show on the light theme too; the tag keeps the fill
        g.fillPath (dashed);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (end));

        // The source's tag half way along.
        const auto mid = cable.getPointAlongPath (cable.getLength() * 0.5f);
        const auto tag = juce::Rectangle<float> (46.0f, 20.0f).withCentre (mid);
        g.setColour (colour);
        g.fillRoundedRectangle (tag, 10.0f);
        g.setColour (p.onAccent);
        g.setFont (uiFont (11.0f, Weight::medium));
        g.drawText (info::sourceLabel (source), tag, juce::Justification::centred, false);
    }

    // What the arc under the pointer is, and how far it goes.
    const bool showHover = hover.target != nullptr && model.getMode() == ModulationModel::Mode::idle;
    if (showHover || editingSlot >= 0)
    {
        const auto& arc = hover.arc;
        const auto text = info::connectionText ({ arc.source, engine::ModDest::none, arc.amount });
        const auto label = info::sourceLabel (arc.source) + " " + info::amountText (arc.amount);
        juce::ignoreUnused (text);

        const auto centre = getLocalPoint (&hover.target->getComponent(), hover.target->getRingCentre());
        const float width = textWidth (uiFont (11.0f, Weight::medium), label) + 16.0f;
        const auto pill = juce::Rectangle<float> (width, 18.0f).withCentre ({ centre.x, centre.y - modArcRadius (hover.position) - 18.0f });
        g.setColour (p.ink);
        g.fillRoundedRectangle (pill, 9.0f);
        g.setColour (p.surface);
        g.setFont (uiFont (11.0f, Weight::medium));
        g.drawText (label, pill, juce::Justification::centred, false);
    }
}

void ModOverlay::mouseMove (const juce::MouseEvent& e)
{
    const auto ring = findRing (e.getPosition());
    const bool changed = ring.target != hover.target || ring.arc.slot != hover.arc.slot;
    hover = ring;

    if (model.getMode() == ModulationModel::Mode::armed)
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else
        setMouseCursor (ring.target != nullptr ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);

    if (changed)
        repaint();
}

void ModOverlay::mouseExit (const juce::MouseEvent&)
{
    if (editingSlot < 0 && hover.target != nullptr)
    {
        hover = {};
        repaint();
    }
}

void ModOverlay::showRingMenu (const ModArc& arc)
{
    juce::PopupMenu menu;
    const auto& c = model.getConnections()[(size_t) arc.slot];
    menu.addItem (1, "Remove " + info::connectionText (c));

    const int slot = arc.slot;
    juce::Component::SafePointer<ModOverlay> safe (this);
    // Targeted at this component so the menu is scaled with the window, and placed at the pointer.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, slot] (int result)
                        {
                            if (safe != nullptr && result == 1)
                                safe->model.getProcessor().disconnectMod (slot);
                        });
}

void ModOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (model.getMode() == ModulationModel::Mode::armed)
    {
        if (auto* t = findValidTarget (e.getPosition()))
            model.dropOn (*t, nullptr);

        return;
    }

    const auto ring = findRing (e.getPosition());

    if (ring.target == nullptr)
        return;

    hover = ring;

    if (e.mods.isPopupMenu())
    {
        showRingMenu (ring.arc);
        return;
    }

    editingSlot = ring.arc.slot;
    editingPercent = ring.arc.amount * 100.0f;
    lastY = e.getPosition().y;
    model.getProcessor().beginModAmountEdit (editingSlot);
    repaint();
}

void ModOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (editingSlot < 0)
        return;

    // Up raises the amount. A pixel is a percent; Shift is four times finer.
    const int dy = e.getPosition().y - lastY;
    lastY = e.getPosition().y;
    editingPercent = juce::jlimit (-100.0f, 100.0f, editingPercent - (float) dy * (e.mods.isShiftDown() ? 0.25f : 1.0f));
    model.getProcessor().setModAmount (editingSlot, editingPercent);
    hover.arc.amount = editingPercent * 0.01f;
    repaint();
}

void ModOverlay::mouseUp (const juce::MouseEvent&)
{
    if (editingSlot < 0)
        return;

    model.getProcessor().endModAmountEdit (editingSlot);
    editingSlot = -1;
    hover = {};
    repaint();
}

} // namespace scratchpad::ui
