#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace scratchpad::ui::a11y
{

/** Says `text` to a screen reader, if one is listening (JUCE's `postAnnouncement`: a UI Automation notification on
    Windows, a VoiceOver announcement on macOS, nothing on Linux). Keep it to one short sentence. Message thread. */
void announce (const juce::String& text,
               juce::AccessibilityHandler::AnnouncementPriority priority = juce::AccessibilityHandler::AnnouncementPriority::medium);

/** Tests listen here: every announcement is also handed to this, so they can see what was said. */
void setAnnouncementSink (std::function<void (const juce::String&)>);

/** A value's text as a screen reader should say it: "+7 c" is "+7 cents", "50%" is "50 percent", "1.20 s" is
    "1.20 seconds". Other text ("2.00 kHz", "-6.0 dB", "Saw") is already fine to read aloud. */
juce::String spokenValue (const juce::String& displayText);

/** "plus 40 percent" style words for a modulation amount given as a fraction, -1 to +1. */
juce::String spokenAmount (float amount);

//==============================================================================
/** The handler for a component that groups others (a card, a bar, a panel): a named group in the screen reader's
    tree. The component also needs `setFocusContainerType (focusContainer)` so its children nest under it. */
std::unique_ptr<juce::AccessibilityHandler> makeGroupHandler (juce::Component&);

/** The handler for a component that only holds others or draws decoration: it adds nothing to the tree, but the
    accessible things inside it still show. */
std::unique_ptr<juce::AccessibilityHandler> makeIgnoredHandler (juce::Component&);

/** A handler whose description is worked out when a screen reader asks, so it is never stale. */
class DescribedHandler : public juce::AccessibilityHandler
{
public:
    DescribedHandler (juce::Component&, juce::AccessibilityRole, std::function<juce::String()> describe,
                      juce::AccessibilityActions = {}, Interfaces = {});

    juce::String getDescription() const override;
    juce::String getHelp() const override { return {}; }

private:
    std::function<juce::String()> describe;
};

/** A read-only value for something that shows a level, such as a meter: a number in a range, and the words for it. */
class ReadOnlyRangeValue final : public juce::AccessibilityValueInterface
{
public:
    ReadOnlyRangeValue (double minimum, double maximum, std::function<double()> current, std::function<juce::String()> text);

    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override { return currentValue(); }
    juce::String getCurrentValueAsString() const override { return valueText(); }
    void setValue (double) override {}
    void setValueAsString (const juce::String&) override {}
    AccessibleValueRange getRange() const override { return { { low, high }, 0.0 }; }

private:
    double low, high;
    std::function<double()> currentValue;
    std::function<juce::String()> valueText;
};

/** A read-only line of text as a value: what a status light or a fixed dial says. */
class ReadOnlyTextValue final : public juce::AccessibilityTextValueInterface
{
public:
    explicit ReadOnlyTextValue (std::function<juce::String()> text) : valueText (std::move (text)) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return valueText(); }
    void setValueAsString (const juce::String&) override {}

private:
    std::function<juce::String()> valueText;
};

/** The value of a segmented selector: the chosen segment's text, which a screen reader user may also set by name. */
class SelectorValue final : public juce::AccessibilityTextValueInterface
{
public:
    SelectorValue (std::function<juce::String()> chosen, std::function<void (const juce::String&)> choose)
        : getChosen (std::move (chosen)), setChosen (std::move (choose)) {}

    bool isReadOnly() const override { return false; }
    juce::String getCurrentValueAsString() const override { return getChosen(); }
    void setValueAsString (const juce::String& text) override { setChosen (text); }

private:
    std::function<juce::String()> getChosen;
    std::function<void (const juce::String&)> setChosen;
};

/** A selector's handler: a combo box named after the control, whose value is the chosen segment. */
std::unique_ptr<juce::AccessibilityHandler> makeSelectorHandler (juce::Component&, std::function<juce::String()> chosen,
                                                                 std::function<void (const juce::String&)> choose,
                                                                 std::function<juce::String()> describe = {});

//==============================================================================
/** Something a screen reader reads but the eye gets from painted text or a picture: an invisible component laid
    over it. It takes no mouse clicks and no keyboard focus.

    `Kind::text` is a line of text (the title is the text); `Kind::image` is a picture with a text alternative (the
    description, worked out on request, so a live picture never describes what it showed before). */
class Region final : public juce::Component
{
public:
    enum class Kind { text, image };

    explicit Region (Kind k = Kind::text) : kind (k) { setInterceptsMouseClicks (false, false); }

    /** The name a screen reader says. Says so to the screen reader if it changed. */
    void setText (const juce::String& text);

    /** For a picture: the alternative text, asked for whenever it is needed. */
    void setDescriber (std::function<juce::String()> describeFn) { describe = std::move (describeFn); }

    /** Where focus goes in Tab order relative to its siblings (see `Component::setExplicitFocusOrder`). */
    using juce::Component::setExplicitFocusOrder;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    Kind kind;
    std::function<juce::String()> describe;
};

/** Makes a picture component's handler: role image, with the alternative text asked for when needed. */
std::unique_ptr<juce::AccessibilityHandler> makeImageHandler (juce::Component&, std::function<juce::String()> describe);

} // namespace scratchpad::ui::a11y
