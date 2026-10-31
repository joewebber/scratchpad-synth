#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/HeaderButtons.h"
#include "ui/Knob.h"
#include "ui/Layout.h"
#include "ui/MorePopover.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** A raised card holding one module's controls. Its header row (`layout::header`) is, left to right: the module's
    handle (a family-coloured dot, or the drag handle of a modulation source), its name, a "?" that pins the Explain
    panel, and at the right edge "B" (bypass), "S" (solo, sources only) and the "..." More button, in that order.
    More opens a pop-over (`MorePopover`) with the controls that do not fit on the card and, last, Remove for a module
    that can be taken out. The card's size is fixed by `layout.h`; a control that does not fit goes behind More.

    To a screen reader a card is a named group (its title is the module's name, its description what the module
    does) holding its controls. Tab visits, in this order: "?", bypass, solo and More (those the card has), then the
    card's own controls in the order its constructor gives (`setFocusOrder`). */
class ModuleCard : public juce::Component
{
public:
    ModuleCard();

    void paint (juce::Graphics&) override;

    /** Names the module for the Explain panel: hovering anywhere on the card explains it, and the "?" button asks. */
    void setExplainKey (const juce::String& moduleKey);

    /** Puts the keyboard on the card's first control (what a newly added module offers first). */
    void focusFirstControl();

    /** Opens or closes the More pop-over, as its button does. */
    void toggleMore();
    bool isMoreOpen() const noexcept { return morePopover != nullptr; }
    MoreButton* getMoreButton() noexcept { return moreButton.get(); }

protected:
    /** The card's own controls in Tab order, after the header buttons. The first is where `focusFirstControl` lands,
        unless `entry` says otherwise. */
    void setFocusOrder (const std::vector<juce::Component*>& controls, juce::Component* entry = nullptr);

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    /** Gives the card a More button. `title` heads the pop-over; `family` colours it; `fill` adds its rows (and runs
        each time it opens, so it can say how things are now). `removeLabel` (with `onRemove`) adds the Remove row
        last; leave it empty for a module that cannot be removed. */
    void makeMore (const juce::String& title, juce::Colour Palette::* family, std::function<void (MorePopover&)> fill,
                   const juce::String& removeLabel = {}, const juce::String& removeAccessibleName = {}, std::function<void()> onRemove = {});

    /** Adds "?" and, for modules that have them, "B" (bypass) and "S" (solo, sources only) for the module
        whose parameters start with `key`. A bypassed card, or a source muted by another's solo, is dimmed. */
    void makeControllable (plugin::ScratchpadProcessor&, const juce::String& key, bool soloable, bool bypassable = true);

    /** Lays the header row out: "?" after the name (which starts `handleWidth` + `layout::header::gap` from the row's
        left edge), then B, S and More at the right edge. Cards call this from `resized`. */
    void placeHeader (juce::Rectangle<int> row, int handleWidth = layout::header::dot);

    /** Handle (a family-coloured dot with a stroked 12 x 10 glyph; none if `glyph` is null, for a card whose handle
        is a component) and then the module name. */
    static void drawHeader (juce::Graphics&, const Palette&, juce::Rectangle<float> row,
                            juce::Colour family, const char* glyph, const juce::String& title,
                            int handleWidth = layout::header::dot);

    /** How much of the card the body fades to when bypassed (the header stays at full strength). */
    static constexpr float bypassedBodyAlpha = 0.4f;

    static constexpr int pad = 8;
    static constexpr int headerHeight = layout::header::height;

private:
    void refreshState();
    void closeMore();

    plugin::ScratchpadProcessor* controlled = nullptr;
    juce::String controlKey;
    bool isSoloable = false;
    std::unique_ptr<MoreButton> moreButton;
    std::unique_ptr<HeaderToggle> explainButton, bypassButton, soloButton;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> stateWatchers;
    juce::Component::SafePointer<juce::Component> entryControl;
    juce::Component::SafePointer<MorePopover> morePopover;
    std::function<void (MorePopover&)> fillMore;
    juce::String moreTitle, moreRemoveLabel, moreRemoveName;
    juce::Colour Palette::* moreFamily = &Palette::src;
    std::function<void()> moreRemove;
};

} // namespace scratchpad::ui
