#pragma once

#include "ui/Accessibility.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModArcs.h"
#include "ui/ModuleCard.h"
#include "ui/ModulationModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace scratchpad::ui
{

/** The round handle a connection starts from. Drag it onto a knob, or click it and then click a knob. Escape (or
    clicking it again) lets go. Disabled while its module is not in the patch.

    From the keyboard, Enter or Space opens a menu of what the source can move (`ModulationModel::chooseDestination`);
    choosing one connects it at +50%. If the handle was armed with a click, Enter or Space puts it down again. */
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

    static constexpr int diameter = 22;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void connectingChanged() override { repaint(); }
    void activate();
    juce::Point<int> centreOnScreen() const;
    bool isConnectingFromHere() const;

    ModulationModel& model;
    const engine::ModSource source;
    bool dragStarted = false;
    bool ignoreClick = false;
};

/** A pill-shaped on/off switch with a word on it: the family colour while on, an outline while off. It is the Sync
    pill in an LFO's header and the Retrig toggle on a full LFO. A switch to a screen reader, bound to a bool
    parameter (one undo step per click, as `ToggleChip`). */
class TogglePill final : public juce::Button
{
public:
    TogglePill (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& labelText,
                const juce::String& accessibleName, juce::UndoManager&);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    /** The size of the Sync pill in an LFO's header. */
    static constexpr int width = 38, height = 20;

private:
    juce::String label;
    juce::UndoManager& undo;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

/** An LFO. In the header its handle ("L1"), name, "?", the Sync pill, B and More; below it a well that draws the
    wave (with a dot that follows its phase) and names the shape, and two knobs: Rate (Beat while Sync is on: one
    knob, whose place and caption the note length takes over) and Fade. It lays itself out from its own box
    (`layout::lfo1`, 214 x 88, is the least): the well takes any extra width and height, the knobs keep their size at
    the bottom right.

    Compact (narrower than `layout::full::lfoWidth`): the shape (as segments) and retrigger are behind More, and
    clicking the well opens it. Full: they are on the card, a strip of the six shapes in two rows and a Retrig
    toggle after the knobs; More then holds only Remove, and the well is only a picture. Tab goes handle, Sync, the
    well (compact only), the knobs, the shape strip and Retrig (full only). */
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

    /** Whether the card is showing its full layout (its box is `layout::full::lfoWidth` wide or more). */
    bool isFull() const noexcept { return getWidth() >= layout::full::lfoWidth; }

private:
    class ShapeWell;

    void liveUpdated() override;
    void applyColours();
    void refreshSync();
    void paintWell (juce::Graphics&, juce::Rectangle<float> bounds) const;
    void describeWell();

    ModulationModel& model;
    const int index;
    SourceHandle handle;
    TogglePill sync;
    Knob rate, division, fade;
    ChoiceSegments shapeStrip;
    TogglePill retrigger;
    std::unique_ptr<ShapeWell> well;
    juce::ParameterAttachment shapeWatcher, syncWatcher;
    int shapeIndex = 0;
    float shownPhase = -1.0f;
    bool secondCycle = false;
    bool synced = false;

    juce::Rectangle<int> headerRow, wellBounds;
};

/** The sources that are not LFOs or envelopes (`layout::playing`, 190 x 88 at the least): "PLAYING" and a 2 x 2 grid
    of handles, each with its name: Velocity, Mod wheel, Aftertouch and Key. They are always in the patch. (The
    envelopes' handles sit on their envelope cards.) It lays itself out from its own width; a taller box (up to 104)
    leaves the chips where they are, at the top, with the extra height empty at the bottom. */
class PlayingCard final : public ModuleCard
{
public:
    PlayingCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Chip
    {
        std::unique_ptr<SourceHandle> handle;
        juce::String name;
        juce::Rectangle<int> labelArea;
    };

    std::array<Chip, 4> chips;
    juce::Rectangle<int> captionRow;
};

/** One connection in the list: source chip, destination, a bar for the amount, the amount and a cross. Drag
    sideways to change the amount, double-click to put it back to +50%, click the cross (or press Delete) to remove it.
    It stands for one of the model's eight slots; with a connection it is a row, without one it is hidden or (in the
    list's columns) a dashed placeholder. It is as wide as its column and the bar takes the width.

    From the keyboard a row takes the focus: Left and Down lower the amount by 1, Right and Up raise it by
    1 (Shift or Page Up and Down: 10); Home or Enter puts it back to +50%; Delete or Backspace removes the connection
    and moves the focus on; Shift+F10 opens the same remove menu as a right-click. To a screen reader a row is a
    slider whose value is the amount, so it can be set directly. */
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
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override { repaint(); }

    /** Called when the slot's connection changes, to refresh text and accessibility. */
    void refresh();

    bool isFilled() const;

    /** Draws a slot that holds no connection as a dashed "Empty slot" placeholder (the list in columns), rather than
        nothing (the list shows only the filled slots). */
    void setShowsEmpty (bool);

    /** Called when the row takes the keyboard focus, so the list can scroll it into view. */
    std::function<void (ConnectionRow&)> onFocused;

    static constexpr int height = 17, removeWidth = 16;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void showRemoveMenu();
    void remove();
    juce::Rectangle<int> removeArea() const { return getLocalBounds().removeFromRight (removeWidth); }
    void nudge (float percent);

    ModulationModel& model;
    const int slot;
    bool overRemove = false;
    bool dragging = false;
    bool showsEmpty = false;
    float amountPercent = 0.0f;
    bool wasActive = false;
    int lastDragX = 0;
};

/** The patch's connections: "CONNECTIONS" and "3 of 8" (of the model's eight slots), then the slots as a list of rows.
    It lays itself out from its own box (`layout::connections`, 262 x 88, is the least). Rows keep their height and
    their text; the amount bar takes whatever width the column has.

    The list shows as many rows to a column as the box is tall enough for, at least three (`designRows`: four at
    `layout::full::connectionsTallHeight`). Narrower than `layout::full::connectionsColumnsWidth` it is one column of
    the filled slots, in slot order; the mouse wheel scrolls past what is in view, and so does Tab (a row that takes
    the focus is brought into view). With none, a line says how to make one. From that width on it is two columns,
    and the empty slots follow the filled ones as dashed "Empty slot" placeholders, so all eight slots are always
    positions in the list; filled and empty alike fill down the first column and then the second (so at four rows to
    a column all eight show with no scrolling, and at fewer the sequence scrolls on by one position per step).
    Tab, and a screen reader, visit the rows in that same order: down the first column, then down the second. */
class ConnectionsPanel final : public ModuleCard,
                               private ModulationModel::Listener
{
public:
    explicit ConnectionsPanel (ModulationModel&);
    ~ConnectionsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** The rows to a column at the least (what the compact box shows). */
    static constexpr int designRows = 3;

    /** Whether the list is in two columns (the box is `layout::full::connectionsColumnsWidth` wide or more). */
    bool isFull() const noexcept { return getWidth() >= layout::full::connectionsColumnsWidth; }

private:
    class List;

    void modulationChanged() override;
    void layoutRows();
    void scrollTo (int firstRow);
    void reveal (ConnectionRow&);
    int filledCount() const;
    int positionCount() const;
    int maxFirstRow() const;

    ModulationModel& model;
    std::array<std::unique_ptr<ConnectionRow>, engine::numModSlots> rows;
    std::unique_ptr<List> list;
    int firstRow = 0;
    int rowsPerColumn = designRows, columns = 1;
    float wheelRemainder = 0.0f;
    juce::Rectangle<int> captionRow, listArea;
};

} // namespace scratchpad::ui
