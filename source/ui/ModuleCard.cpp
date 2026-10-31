#include "ui/ModuleCard.h"

#include "plugin/Parameters.h"
#include "ui/ExplainPanel.h"

#include <cmath>
#include <utility>

namespace scratchpad::ui
{

//==============================================================================
ModuleCard::ModuleCard()
{
    setPaintingIsUnclipped (true);
    setFocusContainerType (FocusContainerType::focusContainer); // so a screen reader nests the controls under their card
}

void ModuleCard::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::small, p.surface);
}

std::unique_ptr<juce::AccessibilityHandler> ModuleCard::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group,
                                                     [this]
                                                     {
                                                         const auto* entry = content::Content::get().module (getProperties()["explainId"].toString());
                                                         return entry != nullptr ? entry->what : juce::String();
                                                     });
}

void ModuleCard::setFocusOrder (const std::vector<juce::Component*>& controls, juce::Component* entry)
{
    int order = 1;

    for (auto* c : std::initializer_list<juce::Component*> { explainButton.get(), bypassButton.get(), soloButton.get(), moreButton.get() })
        if (c != nullptr)
            c->setExplicitFocusOrder (order++);

    entryControl = entry != nullptr ? entry : controls.empty() ? nullptr : controls.front();

    for (auto* c : controls)
        c->setExplicitFocusOrder (order++);
}

void ModuleCard::focusFirstControl()
{
    if (entryControl != nullptr)
        entryControl->grabKeyboardFocus();
}

void ModuleCard::makeMore (const juce::String& title, juce::Colour Palette::* family, std::function<void (MorePopover&)> fill,
                           const juce::String& removeLabel, const juce::String& removeAccessibleName, std::function<void()> onRemove)
{
    moreTitle = title;
    moreFamily = family;
    fillMore = std::move (fill);
    moreRemoveLabel = removeLabel;
    moreRemoveName = removeAccessibleName;
    moreRemove = std::move (onRemove);

    moreButton = std::make_unique<MoreButton> (getTitle().isNotEmpty() ? getTitle() : title);
    moreButton->onClick = [this] { toggleMore(); };
    addAndMakeVisible (*moreButton);
}

void ModuleCard::toggleMore()
{
    if (moreButton == nullptr)
        return;

    if (morePopover != nullptr)
    {
        closeMore();
        return;
    }

    auto popover = std::make_unique<MorePopover> (moreTitle, moreFamily);

    if (fillMore)
        fillMore (*popover);

    if (moreRemoveLabel.isNotEmpty() && moreRemove)
        popover->addRemove (moreRemoveLabel, moreRemoveName.isNotEmpty() ? moreRemoveName : moreRemoveLabel, moreRemove);

    morePopover = popover.get();
    moreButton->setOpen (true);

    popover->onClosed = [safe = juce::Component::SafePointer<ModuleCard> (this)]
    {
        if (safe != nullptr && safe->moreButton != nullptr)
            safe->moreButton->setOpen (false);
    };

    MorePopover::show (std::move (popover), *moreButton);
}

void ModuleCard::closeMore()
{
    if (morePopover != nullptr)
        morePopover->close();
}

void ModuleCard::placeHeader (juce::Rectangle<int> row, int handleWidth)
{
    namespace h = layout::header;

    // Right edge, right to left: More, S, B.
    const auto place = [&] (juce::Component* c)
    {
        if (c != nullptr)
        {
            c->setBounds (row.removeFromRight (h::buttonWidth).withSizeKeepingCentre (h::buttonWidth, h::buttonHeight));
            row.removeFromRight (h::buttonGap);
        }
    };

    place (moreButton.get());
    place (soloButton.get());
    place (bypassButton.get());

    // The "?" follows the name.
    if (explainButton != nullptr)
    {
        const int nameWidth = (int) std::ceil (textWidth (uiFont (h::nameSize, Weight::medium), getTitle()));
        const int x = row.getX() + handleWidth + h::gap + nameWidth + h::gap;
        explainButton->setBounds (juce::Rectangle<int> (x, row.getY(), h::explain, h::explain).withCentre ({ x + h::explain / 2, row.getCentreY() }));
    }
}

void ModuleCard::setExplainKey (const juce::String& moduleKey)
{
    getProperties().set ("explainId", moduleKey);
}

void ModuleCard::makeControllable (plugin::ScratchpadProcessor& p, const juce::String& key, bool soloable, bool bypassable)
{
    controlled = &p;
    controlKey = key;
    isSoloable = soloable;
    setExplainKey (key);

    explainButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::explain, "Explain " + getTitle());
    explainButton->setDescription ("Shows what the " + getTitle() + " is and does in the Explain panel, and keeps it there.");
    explainButton->onClick = [this] { ExplainPanel::pinFrom (*this, controlKey); };
    explainButton->getProperties().set ("explainId", key);
    addAndMakeVisible (*explainButton);

    if (bypassable)
    {
        bypassButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::bypass, getTitle() + " bypass");
        bypassButton->onClick = [this] { controlled->setBypassed (controlKey, ! controlled->isBypassed (controlKey)); };
        addAndMakeVisible (*bypassButton);
        stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
            *p.apvts.getParameter (key + ".bypass"), [this] (float) { refreshState(); }));
    }

    if (soloable)
    {
        soloButton = std::make_unique<HeaderToggle> (HeaderToggle::Kind::solo, getTitle() + " solo");
        soloButton->onClick = [this] { controlled->setSoloed (controlKey, ! controlled->isSoloed (controlKey)); };
        addAndMakeVisible (*soloButton);

        // Any source's solo changes whether this one is heard.
        for (int i = 0; i < params::numSoloable; ++i)
            stateWatchers.push_back (std::make_unique<juce::ParameterAttachment> (
                *p.apvts.getParameter (juce::String (params::bypassKeys[(size_t) i]) + ".solo"), [this] (float) { refreshState(); }));
    }

    refreshState();
}

void ModuleCard::refreshState()
{
    if (controlled == nullptr)
        return;

    const bool bypassed = bypassButton != nullptr && controlled->isBypassed (controlKey);
    const bool soloed = isSoloable && controlled->isSoloed (controlKey);

    bool anySolo = false;
    for (int i = 0; i < params::numSoloable; ++i)
        anySolo = anySolo || controlled->isSoloed (params::bypassKeys[(size_t) i]);

    if (bypassButton != nullptr)
        bypassButton->setOn (bypassed);

    if (soloButton != nullptr)
        soloButton->setOn (soloed);

    // A bypassed module, or a source another one's solo has muted: the header stays at full strength (the buttons
    // that undo it must be easy to see) and the body fades.
    const bool faded = bypassed || (isSoloable && anySolo && ! soloed);

    for (auto* child : getChildren())
        if (child != explainButton.get() && child != bypassButton.get() && child != soloButton.get() && child != moreButton.get())
            child->setAlpha (faded ? bypassedBodyAlpha : 1.0f);
}

void ModuleCard::drawHeader (juce::Graphics& g, const Palette& p, juce::Rectangle<float> row,
                             juce::Colour family, const char* glyph, const juce::String& title, int handleWidth)
{
    namespace h = layout::header;

    if (glyph != nullptr)
    {
        const auto dot = juce::Rectangle<float> ((float) h::dot, (float) h::dot).withCentre ({ row.getX() + (float) h::dot * 0.5f, row.getCentreY() });
        g.setColour (family);
        g.fillEllipse (dot);

        auto path = juce::Drawable::parseSVGPath (glyph);
        path.applyTransform (juce::AffineTransform::translation (dot.getX() + 4.0f, dot.getY() + 5.0f));
        g.setColour (p.onAccent);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    g.setColour (p.ink);
    g.setFont (uiFont (h::nameSize, Weight::medium));
    g.drawText (title, row.withTrimmedLeft ((float) (handleWidth + h::gap)), juce::Justification::centredLeft, false);
}

} // namespace scratchpad::ui
