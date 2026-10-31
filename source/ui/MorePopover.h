#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/Layout.h"
#include "ui/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** The "..." button at the right end of a card's header (22 x 20): opens the card's More pop-over. Inverted fill
    while open. A small dot marks a card whose pop-over holds something doing something (`inUse`, calm) or something
    to watch for (`warning`, the accent colour: anti-aliasing is off). To a screen reader it is a toggle button
    named "<module> more options", pressed while the pop-over is open. */
class MoreButton final : public juce::Button
{
public:
    enum class Marker { none, inUse, warning };

    explicit MoreButton (const juce::String& moduleName);

    void setOpen (bool shouldBeOpen);
    void setMarker (Marker);
    bool isOpen() const noexcept { return open; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void refreshTitle();

    juce::String name;
    bool open = false;
    Marker marker = Marker::none;
};

/** The content of a card's More pop-over (`layout::popover`): a title row with the card's family dot, then rows
    separated by hairlines - switches, wrapped segmented controls, knobs (label and value both shown) and, last,
    Remove. Rows are added in order, then `show` puts it next to the button that opened it; it is laid out at the
    height its rows need. Every control is bound to its parameter and changes it as it is touched, as it does on a
    card. The pop-over closes on a click outside it, on Escape and on its close button, and then calls `onClosed`. */
class MorePopover final : public juce::Component
{
public:
    /** One knob in a knobs row. `setup` runs after the knob is made (modulation, bipolar, family colour...). */
    struct KnobSpec
    {
        juce::String paramId, caption, accessibleName;
        std::function<void (Knob&)> setup;
    };

    MorePopover (const juce::String& moduleTitle, juce::Colour Palette::* family);
    ~MorePopover() override;

    /** A switch bound to a bool parameter, with an optional line of explanation under it. */
    void addSwitch (plugin::ScratchpadProcessor&, const juce::String& paramId, const juce::String& label,
                    const juce::String& accessibleName, const juce::String& note = {});

    /** A choice parameter as a wrapped row of segments (`columns` to a row; 0 is one row). */
    void addSegments (plugin::ScratchpadProcessor&, const juce::String& paramId, const juce::String& label,
                      const juce::String& accessibleName, int columns, const juce::StringArray& displayWords = {});

    /** One row of segments for two parameters: a bool that turns the thing on and a choice for which kind. The first
        segment is "off" (the bool false); the others are the choice's values, in order (`words` names all of them,
        the off segment first). One pick is one undo step. */
    void addOffChoice (plugin::ScratchpadProcessor&, const juce::String& enabledId, const juce::String& modeId,
                       const juce::String& label, const juce::String& accessibleName, int columns, const juce::StringArray& words);

    void addKnobs (plugin::ScratchpadProcessor&, const juce::String& label, const std::vector<KnobSpec>&, const juce::String& note = {});

    /** A line of text of its own: what the controls above are doing, or why one is dimmed (as it is when opened). */
    void addNote (const juce::String& text);

    /** The last row: "Remove ...". Pressing it closes the pop-over and then calls `onRemove`. */
    void addRemove (const juce::String& label, const juce::String& accessibleName, std::function<void()> onRemove);

    /** A control by the parameter it is bound to, for a card that dims one as the patch changes. Null if none. */
    juce::Component* findControl (const juce::String& paramId) const;

    /** Called once when the pop-over has gone. */
    std::function<void()> onClosed;

    /** Opens it beside `anchor` (the More button), inside the editor's scaled stage. The keyboard goes back to `anchor` when it
        closes. */
    static void show (std::unique_ptr<MorePopover> popover, juce::Component& anchor);

    void close();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The height the rows need at `layout::popover::width`. */
    int heightForRows() const;

private:
    class Row;
    class SwitchRow;
    class SegmentsRow;
    class KnobsRow;
    class RemoveRow;
    class NoteRow;
    class OffChoiceRow;

    void addRow (std::unique_ptr<Row>);

    juce::String title;
    juce::Colour Palette::* family;
    std::vector<std::unique_ptr<Row>> rows;
    std::unique_ptr<juce::Button> closeButton;
    juce::CallOutBox* box = nullptr;
    juce::Rectangle<int> headerRow;
};

} // namespace scratchpad::ui
