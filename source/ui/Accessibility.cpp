#include "ui/Accessibility.h"

namespace scratchpad::ui::a11y
{

namespace
{
    std::function<void (const juce::String&)>& sink()
    {
        static std::function<void (const juce::String&)> instance;
        return instance;
    }
}

void announce (const juce::String& text, juce::AccessibilityHandler::AnnouncementPriority priority)
{
    if (text.isEmpty())
        return;

    if (sink() != nullptr)
        sink() (text);

    juce::AccessibilityHandler::postAnnouncement (text, priority);
}

void setAnnouncementSink (std::function<void (const juce::String&)> newSink)
{
    sink() = std::move (newSink);
}

juce::String spokenValue (const juce::String& displayText)
{
    auto text = displayText.trim();

    if (text.endsWith (" c"))
        return text.dropLastCharacters (2) + " cents";

    if (text.endsWith ("%"))
        return text.dropLastCharacters (1) + " percent";

    if (text.endsWith (" s"))
        return text.dropLastCharacters (2) + " seconds";

    if (text.startsWith (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"))))
        return "plus or minus " + text.substring (1);

    const auto minusInfinity = juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92\xe2\x88\x9e"));

    if (text.startsWith (minusInfinity))
        return "minus infinity" + text.substring (minusInfinity.length());

    return text;
}

juce::String spokenAmount (float amount)
{
    const int percent = juce::roundToInt (amount * 100.0f);
    return (percent > 0 ? "plus " : percent < 0 ? "minus " : "") + juce::String (std::abs (percent)) + " percent";
}

//==============================================================================
std::unique_ptr<juce::AccessibilityHandler> makeGroupHandler (juce::Component& c)
{
    return std::make_unique<juce::AccessibilityHandler> (c, juce::AccessibilityRole::group);
}

std::unique_ptr<juce::AccessibilityHandler> makeIgnoredHandler (juce::Component& c)
{
    return std::make_unique<juce::AccessibilityHandler> (c, juce::AccessibilityRole::ignored);
}

DescribedHandler::DescribedHandler (juce::Component& c, juce::AccessibilityRole role, std::function<juce::String()> describeFn,
                                    juce::AccessibilityActions actions, Interfaces interfaces)
    : juce::AccessibilityHandler (c, role, std::move (actions), std::move (interfaces)), describe (std::move (describeFn))
{
}

juce::String DescribedHandler::getDescription() const
{
    return describe != nullptr ? describe() : juce::AccessibilityHandler::getDescription();
}

ReadOnlyRangeValue::ReadOnlyRangeValue (double minimum, double maximum, std::function<double()> current,
                                        std::function<juce::String()> text)
    : low (minimum), high (maximum), currentValue (std::move (current)), valueText (std::move (text))
{
}

std::unique_ptr<juce::AccessibilityHandler> makeSelectorHandler (juce::Component& c, std::function<juce::String()> chosen,
                                                                 std::function<void (const juce::String&)> choose,
                                                                 std::function<juce::String()> describe)
{
    return std::make_unique<DescribedHandler> (c, juce::AccessibilityRole::comboBox, std::move (describe),
                                               juce::AccessibilityActions {},
                                               juce::AccessibilityHandler::Interfaces {
                                                   std::make_unique<SelectorValue> (std::move (chosen), std::move (choose)) });
}

std::unique_ptr<juce::AccessibilityHandler> makeImageHandler (juce::Component& c, std::function<juce::String()> describe)
{
    return std::make_unique<DescribedHandler> (c, juce::AccessibilityRole::image, std::move (describe));
}

//==============================================================================
void Region::setText (const juce::String& text)
{
    if (getTitle() == text)
        return;

    setTitle (text);

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
}

std::unique_ptr<juce::AccessibilityHandler> Region::createAccessibilityHandler()
{
    return std::make_unique<DescribedHandler> (*this, kind == Kind::image ? juce::AccessibilityRole::image : juce::AccessibilityRole::staticText,
                                               describe);
}

} // namespace scratchpad::ui::a11y
