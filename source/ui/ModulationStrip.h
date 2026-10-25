#pragma once

#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModArcs.h"
#include "ui/ModuleCards.h"
#include "ui/ModulationModel.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace scratchpad::ui
{

/** The round handle a connection starts from. Drag it onto a knob, or click it and then click a
    knob. Escape (or clicking it again) lets go. Disabled while its module is not in the patch. */
class SourceHandle final : public juce::Component,
                           private ModulationModel::Listener
{
public:
    SourceHandle (ModulationModel&, engine::ModSource);
    ~SourceHandle() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void enablementChanged() override { repaint(); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    engine::ModSource getSource() const noexcept { return source; }

    static constexpr int diameter = 26;

private:
    void connectingChanged() override { repaint(); }
    juce::Point<int> centreOnScreen() const;
    bool isConnectingFromHere() const;

    ModulationModel& model;
    const engine::ModSource source;
    bool dragStarted = false;
    bool ignoreClick = false;
};

/** An LFO: handle, shape, rate, note length, fade-in, retrigger, tempo sync, and a picture of the wave with a
    dot that follows its phase. With Sync on the rate follows the tempo (the note length picks how many beats a
    cycle takes), so the Rate knob dims and the picture shows the rate that results. */
class LfoCard final : public ModuleCard,
                      private ModulationModel::Listener
{
public:
    LfoCard (plugin::ScratchpadProcessor&, ModulationModel&, int lfoIndex);
    ~LfoCard() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    SourceHandle& getHandle() noexcept { return handle; }

    static constexpr int width = 288, height = 116;

private:
    void liveUpdated() override;
    void applyColours();
    void refreshSync();

    ModulationModel& model;
    const int index;
    SourceHandle handle;
    ChoiceSegments shape;
    ToggleChip retrigger, sync;
    Knob rate, division, fade;
    juce::ParameterAttachment shapeWatcher, syncWatcher, divisionWatcher;
    int shapeIndex = 0;
    float shownPhase = -1.0f;
    bool secondCycle = false;
    bool synced = false;
    int divisionIndex = 0;
    float shownHz = -1.0f;

    juce::Rectangle<int> headerRow, vizWell;
};

/** Handles for the sources that are not LFOs: the two envelopes, velocity, mod wheel,
    aftertouch and key. */
class SourcesPanel final : public ModuleCard
{
public:
    SourcesPanel (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Envelope handles only work while their module is in the patch. */
    void refreshPresence();

    static constexpr int width = 232, height = 116;

private:
    struct Chip
    {
        std::unique_ptr<SourceHandle> handle;
        juce::String name;
        juce::Rectangle<int> area;
    };

    plugin::ScratchpadProcessor& processor;
    std::array<Chip, 6> chips;
    juce::Rectangle<int> captionRow;
};

/** One of the eight connection slots: source, destination and amount. Drag sideways to change
    the amount, double-click to put it back to +50%, click the cross (or press Delete) to remove it. */
class ConnectionRow final : public juce::Component
{
public:
    ConnectionRow (ModulationModel&, int slotIndex);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    /** Called when the slot's connection changes, to refresh text and accessibility. */
    void refresh();

    static constexpr int height = 24;

private:
    juce::Rectangle<int> removeArea() const { return getLocalBounds().removeFromRight (22); }
    void nudge (float percent);

    ModulationModel& model;
    const int slot;
    bool overRemove = false;
    bool dragging = false;
    int lastDragX = 0;
    float amountPercent = 0.0f;
};

/** All eight slots at a glance, two columns of four. */
class ConnectionsPanel final : public ModuleCard,
                               private ModulationModel::Listener
{
public:
    explicit ConnectionsPanel (ModulationModel&);
    ~ConnectionsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 320, height = 116;

private:
    void modulationChanged() override;

    ModulationModel& model;
    std::array<std::unique_ptr<ConnectionRow>, engine::numModSlots> rows;
    juce::Rectangle<int> captionRow;
};

} // namespace scratchpad::ui
