#pragma once

#include "ui/ModulationModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** A transparent layer over the whole signal chain. While a connection is being made it draws the
    cable and lights the knobs it can reach; while one is armed it takes the click on a knob.
    At rest it lets everything through except the arcs around modulated knobs, which it owns:
    drag an arc to change the amount, right-click it to remove the connection. */
class ModOverlay final : public juce::Component,
                         private ModulationModel::Listener
{
public:
    explicit ModOverlay (ModulationModel&);
    ~ModOverlay() override;

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct RingHit
    {
        ModTarget* target = nullptr;
        ModArc arc {};
        size_t position = 0; // which concentric arc
    };

    void connectingChanged() override { repaint(); }
    void modulationChanged() override { repaint(); }

    RingHit findRing (juce::Point<int> overlayPoint) const;
    ModTarget* findValidTarget (juce::Point<int> overlayPoint) const;
    void showRingMenu (const ModArc&);

    ModulationModel& model;
    RingHit hover;
    int editingSlot = -1;
    float editingPercent = 0.0f;
    int lastY = 0;
};

} // namespace scratchpad::ui
