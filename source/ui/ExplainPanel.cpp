#include "ui/ExplainPanel.h"

#include "ui/Neumorph.h"
#include "ui/Typography.h"

#include <algorithm>
#include <memory>

namespace scratchpad::ui
{

namespace
{
    constexpr int pad = 14;
    constexpr int patchHeight = 112;
    constexpr int scopeHeight = 262;
    constexpr float lineHeight = 16.0f;

    bool isModuleKey (const juce::String& id) { return ! id.containsChar ('.'); }
}

/** The text being shown, as something the keyboard can land on and a screen reader can read: an invisible area over
    the painted text. Up, Down, Page Up, Page Down, Home and End scroll it. */
class ExplainPanel::Body final : public juce::Component
{
public:
    explicit Body (ExplainPanel& panel) : owner (panel)
    {
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (false, false);
        setPaintingIsUnclipped (true);
    }

    void setText (const juce::String& text)
    {
        if (getTitle() == text)
            return;

        setTitle (text);

        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
    }

    void paint (juce::Graphics& g) override
    {
        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat().reduced (3.0f), 8.0f, paletteOf (*this).ink);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getModifiers().isAnyModifierKeyDown())
            return false;

        const float page = (float) juce::jmax (40, getHeight() - 24);
        const int code = key.getKeyCode();

        if (code == juce::KeyPress::downKey)        owner.scrollBy (lineHeight * 2.0f);
        else if (code == juce::KeyPress::upKey)     owner.scrollBy (-lineHeight * 2.0f);
        else if (code == juce::KeyPress::pageDownKey) owner.scrollBy (page);
        else if (code == juce::KeyPress::pageUpKey)   owner.scrollBy (-page);
        else if (code == juce::KeyPress::homeKey)   owner.scrollBy (-1.0e6f);
        else if (code == juce::KeyPress::endKey)    owner.scrollBy (1.0e6f);
        else
            return false;

        return true;
    }

    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
    }

    ExplainPanel& owner;
};

/** A glossary link or the "All terms" chip as a real button, so Tab reaches it and Enter opens it. It paints only its
    focus ring (the chip itself is painted by the panel) and takes no mouse clicks (the panel finds the chip under
    the pointer). The panel lays these over its painted chips, and moves them when it scrolls. */
class ExplainPanel::LinkButton final : public juce::Button
{
public:
    explicit LinkButton (ExplainPanel& panel) : juce::Button ("Glossary link"), owner (panel)
    {
        setPaintingIsUnclipped (true);
        setInterceptsMouseClicks (false, false);
        getProperties().set ("explainId", "control.glossaryLink");
        onClick = [this] { owner.openLink (linkIndex); };
    }

    void setLink (int index, const juce::String& label)
    {
        linkIndex = index;
        setTitle (label);
        setDescription (index < 0 ? "Goes back to the list of glossary terms." : "Opens what the word " + label + " means.");
    }

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), (float) juce::jmin (11, getHeight() / 2), paletteOf (*this).ink);
    }

    void focusGained (FocusChangeType type) override
    {
        juce::Button::focusGained (type);
        owner.revealLink (linkIndex);
    }

private:
    ExplainPanel& owner;
    int linkIndex = 0;
};

ExplainPanel::ExplainPanel (plugin::ScratchpadProcessor& p)
    : processor (p), scope (p), patchText (p.describePatch())
{
    setComponentID ("explainPanel");
    setTitle ("Explain");
    setPaintingIsUnclipped (true);
    setFocusContainerType (FocusContainerType::focusContainer);
    body = std::make_unique<Body> (*this);

    addAndMakeVisible (scope);

    glossaryButton.setTitle ("Glossary");
    glossaryButton.setPaintingIsUnclipped (true);
    glossaryButton.getProperties().set (buttonProps::radius, 11.0);
    glossaryButton.setDescription ("Opens the list of every term the explanations use, in plain English. Press it again to close it.");
    glossaryButton.onClick = [this]
    {
        if (glossaryOpen)
            closeGlossary();
        else
            showGlossary ({});
    };
    addAndMakeVisible (glossaryButton);

    for (auto* b : { &foldButton, &unfoldButton })
    {
        b->getProperties().set ("explainId", "control.explainToggle");
        b->onClick = [this]
        {
            if (onToggleRequested)
                onToggleRequested();
        };
    }

    addAndMakeVisible (foldButton);
    addChildComponent (unfoldButton);
    addAndMakeVisible (*body);
    addAndMakeVisible (patchRegion);

    // Tab, and the screen reader's order: the header buttons, the text and its links, the patch, then the scope.
    glossaryButton.setExplicitFocusOrder (1);
    unfoldButton.setExplicitFocusOrder (1);
    foldButton.setExplicitFocusOrder (2);
    body->setExplicitFocusOrder (3);
    patchRegion.setExplicitFocusOrder (1000);
    scope.setExplicitFocusOrder (1001);
    refreshReadableText();

    // Hear every pointer move in the window, so any control can explain itself without knowing about this panel.
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    juce::Desktop::getInstance().addFocusChangeListener (this);
}

ExplainPanel::~ExplainPanel()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    juce::Desktop::getInstance().removeFocusChangeListener (this);
}

void ExplainPanel::visibilityChanged()
{
    if (isVisible())
        startTimerHz (5);
    else
        stopTimer();
}

void ExplainPanel::setCollapsed (bool shouldCollapse)
{
    if (collapsed == shouldCollapse)
        return;

    collapsed = shouldCollapse;
    scope.setVisible (! collapsed); // so its timer stops while nobody can see it
    glossaryButton.setVisible (! collapsed);
    foldButton.setVisible (! collapsed);
    unfoldButton.setVisible (collapsed);
    resized();
    repaint();
}

void ExplainPanel::focusToggleButton()
{
    (collapsed ? unfoldButton : foldButton).grabKeyboardFocus();
}

void ExplainPanel::pinFrom (const juce::Component& from, const juce::String& moduleKey)
{
    // The panel is somewhere below the window's top-level component, not necessarily a direct child.
    const auto find = [] (auto&& self, juce::Component& parent) -> ExplainPanel*
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* panel = dynamic_cast<ExplainPanel*> (child))
                return panel;

            if (auto* found = self (self, *child))
                return found;
        }

        return nullptr;
    };

    if (auto* top = from.getTopLevelComponent())
        if (auto* panel = find (find, *top))
        {
            panel->show (moduleKey, true);

            if (panel->collapsed && panel->onToggleRequested)
                panel->onToggleRequested();
        }
}

juce::String ExplainPanel::explainIdOf (const juce::Component* c)
{
    for (; c != nullptr; c = c->getParentComponent())
        if (const auto id = c->getProperties()["explainId"].toString(); id.isNotEmpty())
            return id;

    return {};
}

void ExplainPanel::pointAt (const juce::Component* c)
{
    if (glossaryOpen || c == nullptr)
        return;

    const auto id = explainIdOf (c);

    if (id.isEmpty() || id == shownId)
        return;

    // A pinned module stays until the pointer lands on a control (a parameter), which is more specific.
    if (pinned && isModuleKey (id))
        return;

    show (id, false);
}

void ExplainPanel::mouseMove (const juce::MouseEvent& e)
{
    auto* top = getTopLevelComponent();

    if (top != nullptr && e.eventComponent != nullptr && top->isParentOf (e.eventComponent))
        pointAt (e.eventComponent);
}

void ExplainPanel::closeGlossary()
{
    glossaryOpen = false;
    glossaryTerm.clear();
    refreshReadableText();
    collectLinks();
    repaint();
}

void ExplainPanel::refreshReadableText()
{
    const auto& content = content::Content::get();
    juce::String text;

    if (glossaryOpen)
    {
        if (glossaryTerm.isEmpty())
        {
            text = "Glossary. " + juce::String ((int) content.glossaryInOrder().size()) + " terms.";
            for (const auto* term : content.glossaryInOrder())
                text += " " + term->term;
        }
        else if (const auto* term = content.glossary (glossaryTerm))
        {
            text = term->term + ". " + term->definition;
        }
    }
    else
    {
        const content::Entry* entry = content.param (shownId);
        if (entry == nullptr)
            entry = content.module (shownId);
        if (entry == nullptr)
            entry = content.control (shownId);

        if (entry != nullptr)
        {
            text = entry->title;
            if (entry->explainer.isNotEmpty())
                text += ". " + entry->explainer;
            text += ". What it does: " + entry->what + ". Listen for: " + entry->listenFor + ". Try this: " + entry->tryThis;
        }
    }

    body->setText (text.trim());
    patchRegion.setText ("Patch: " + patchText);
}

juce::String ExplainPanel::getReadableText() const
{
    return body != nullptr ? body->getTitle() : juce::String();
}

void ExplainPanel::collectLinks()
{
    const auto& content = content::Content::get();
    juce::StringArray ids;

    if (glossaryOpen)
    {
        if (glossaryTerm.isEmpty())
        {
            for (const auto* term : content.glossaryInOrder())
                ids.add (term->id);
        }
        else if (const auto* term = content.glossary (glossaryTerm))
        {
            ids = term->seeAlso;
        }
    }
    else
    {
        const content::Entry* entry = content.param (shownId);
        if (entry == nullptr)
            entry = content.module (shownId);
        if (entry == nullptr)
            entry = content.control (shownId);
        if (entry != nullptr)
            ids = entry->glossaryTerms;
    }

    for (auto& button : linkButtons)
        button->setVisible (false);
    linkButtons.clear();

    if (glossaryOpen && glossaryTerm.isNotEmpty())
    {
        auto button = std::make_unique<LinkButton> (*this);
        button->setLink (-1, "All terms");
        button->setExplicitFocusOrder (4);
        addAndMakeVisible (*button);
        linkButtons.push_back (std::move (button));
    }

    for (int i = 0; i < ids.size(); ++i)
    {
        const auto* term = content.glossary (ids[i]);
        if (term == nullptr)
            continue;

        auto button = std::make_unique<LinkButton> (*this);
        button->setLink (i, term->term);
        button->setExplicitFocusOrder (5 + i);
        addAndMakeVisible (*button);
        linkButtons.push_back (std::move (button));
    }

    syncLinks();
}

void ExplainPanel::syncLinks()
{
    const auto& content = content::Content::get();

    for (auto& button : linkButtons)
    {
        if (button->getTitle() == "All terms")
        {
            button->setBounds (backArea);
            button->setVisible (glossaryOpen && glossaryTerm.isNotEmpty() && ! backArea.isEmpty());
            continue;
        }

        juce::Rectangle<int> area;
        int index = 0;
        for (const auto& link : links)
        {
            if (const auto* term = content.glossary (link.glossaryId); term != nullptr && term->term == button->getTitle())
            {
                area = link.area;
                button->setLink (index, term->term);
                break;
            }
            ++index;
        }

        button->setBounds (area);
        button->setVisible (! area.isEmpty());
    }
}

void ExplainPanel::scrollBy (float pixels)
{
    if (glossaryOpen)
    {
        const float visible = (float) (bodyArea.withBottom (patchArea.getBottom()).getHeight() - 22);
        glossaryScroll = juce::jlimit (0.0f, std::max (0.0f, glossaryContentHeight - visible), glossaryScroll + pixels);
    }
    else
    {
        entryScroll = juce::jlimit (0.0f, entryMaxScroll, entryScroll + pixels);
    }

    repaint();
}

void ExplainPanel::revealLink (int index)
{
    if (index < 0 || index >= (int) links.size())
        return;

    const auto area = links[(size_t) index].area;
    const auto visible = bodyArea.withBottom (patchArea.getBottom());
    if (area.getY() < visible.getY())
        scrollBy ((float) area.getY() - (float) visible.getY());
    else if (area.getBottom() > visible.getBottom())
        scrollBy ((float) area.getBottom() - (float) visible.getBottom());
}

void ExplainPanel::openLink (int index)
{
    if (index < 0)
    {
        showGlossary ({});
        return;
    }

    if (juce::isPositiveAndBelow (index, (int) links.size()))
        showGlossary (links[(size_t) index].glossaryId);
}

bool ExplainPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && glossaryOpen)
    {
        closeGlossary();
        return true;
    }

    return false;
}

void ExplainPanel::globalFocusChanged (juce::Component* focused)
{
    if (focused != nullptr && getTopLevelComponent()->isParentOf (focused))
        pointAt (focused);
}

void ExplainPanel::show (const juce::String& id, bool pin)
{
    const auto& content = content::Content::get();

    if (content.param (id) == nullptr && content.module (id) == nullptr && content.control (id) == nullptr)
        return;

    if (id != shownId)
        entryScroll = 0.0f;

    shownId = id;
    pinned = pin;
    glossaryOpen = false;
    refreshReadableText();
    collectLinks();
    repaint();
}

void ExplainPanel::showGlossary (const juce::String& termId)
{
    glossaryOpen = true;
    glossaryTerm = termId;
    glossaryScroll = 0.0f;
    refreshReadableText();
    collectLinks();
    repaint();
}

void ExplainPanel::timerCallback()
{
    if (collapsed)
        return;

    const auto text = processor.describePatch();

    if (text != patchText)
    {
        patchText = text;
        repaint (patchArea);
    }
}

void ExplainPanel::resized()
{
    if (collapsed)
    {
        // The rail: the button that brings the panel back, with its name running down under it.
        constexpr int button = 32;
        unfoldButton.setBounds ((getWidth() - button) / 2, pad, button, button);
        headerRow = backArea = bodyArea = patchArea = scopeArea = {};
        return;
    }

    auto area = getLocalBounds().reduced (pad, pad);

    headerRow = area.removeFromTop (24);
    foldButton.setBounds (headerRow.removeFromRight (24));
    headerRow.removeFromRight (6);
    glossaryButton.setBounds (headerRow.removeFromRight (84));

    scopeArea = area.removeFromBottom (scopeHeight);
    scope.setBounds (scopeArea);
    area.removeFromBottom (10);
    patchArea = area.removeFromBottom (patchHeight);
    area.removeFromBottom (6);
    area.removeFromTop (8);
    bodyArea = area;
}

juce::Colour ExplainPanel::familyOf (const juce::String& key, bool ink) const
{
    const auto& p = paletteOf (*this);
    const bool src = key.startsWith ("osc") || key == "noise";
    const bool flt = key == "filter" || key == "filterEnv";
    const bool amp = key == "ampEnv" || key == "amp";
    const bool mod = key.startsWith ("lfo") || key == "connections" || key == "sources";
    const bool fx = key == "distortion" || key == "chorus" || key == "delay" || key == "reverb";

    if (src) return ink ? p.srcInk : p.src;
    if (flt) return ink ? p.fltInk : p.flt;
    if (amp) return ink ? p.ampInk : p.amp;
    if (mod) return ink ? p.modInk : p.mod;
    if (fx) return ink ? p.fxInk : p.fx;
    return ink ? p.perfInk : p.perf;
}

float ExplainPanel::drawParagraph (juce::Graphics& g, const juce::String& heading, const juce::String& text,
                                   juce::Rectangle<float>& area, juce::Colour ink)
{
    const auto& p = paletteOf (*this);
    const float start = area.getY();

    if (heading.isNotEmpty())
    {
        drawCaption (g, heading, area.removeFromTop (14.0f), p.ink2);
        area.removeFromTop (2.0f);
    }

    const auto font = uiFont (12.0f);
    const auto lines = wrapLines (font, text, area.getWidth());
    g.setFont (font);
    g.setColour (ink);
    drawLines (g, lines, area.getPosition(), area.getWidth(), lineHeight);
    area.removeFromTop ((float) lines.size() * lineHeight + 10.0f);
    return area.getY() - start;
}

void ExplainPanel::drawLinks (juce::Graphics& g, const juce::StringArray& terms, juce::Rectangle<float>& area)
{
    if (terms.isEmpty())
        return;

    const auto& p = paletteOf (*this);
    const auto& content = content::Content::get();

    drawCaption (g, "Words used", area.removeFromTop (14.0f), p.ink2);
    area.removeFromTop (4.0f);

    const auto font = uiFont (12.0f);
    float x = area.getX(), y = area.getY();

    for (const auto& id : terms)
    {
        const auto* term = content.glossary (id);

        if (term == nullptr)
            continue;

        const float w = textWidth (font, term->term) + 16.0f;

        if (x + w > area.getRight())
        {
            x = area.getX();
            y += 26.0f;
        }

        const juce::Rectangle<float> chip (x, y, w, 22.0f);
        g.setColour (p.accentSoft);
        g.fillRoundedRectangle (chip, 11.0f);
        g.setColour (p.ink);
        g.setFont (font);
        g.drawText (term->term, chip, juce::Justification::centred, false);

        links.push_back ({ chip.toNearestInt(), id, term->term });
        x += w + 6.0f;
    }

    area.setY (y + 28.0f);
}

void ExplainPanel::paintEntry (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto& p = paletteOf (*this);
    const auto& content = content::Content::get();

    const auto* entry = content.param (shownId);
    const bool isControl = entry == nullptr && content.control (shownId) != nullptr;
    const bool isModule = entry == nullptr && ! isControl;

    if (isControl)
        entry = content.control (shownId);
    else if (isModule)
        entry = content.module (shownId);

    if (entry == nullptr)
        return;

    const auto moduleKey = isModule ? shownId : entry->module;
    const auto* module = content.module (moduleKey);
    auto area = bounds.toFloat();
    const auto top = area.getY();

    // Kicker: a dot in the module's family colour, then where this sits.
    auto kicker = area.removeFromTop (16.0f);
    g.setColour (familyOf (moduleKey, false));
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ kicker.getX() + 5.0f, kicker.getCentreY() }));
    kicker.removeFromLeft (16.0f);

    const juce::String dotSeparator (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    auto kickerText = isModule ? juce::String ("Module") : isControl ? juce::String ("Control") : (module != nullptr ? module->title : juce::String());
    if (! isModule && ! isControl)
        kickerText += dotSeparator + entry->title;
    if (pinned && isModule)
        kickerText += dotSeparator + "pinned";
    drawCaption (g, kickerText, kicker, familyOf (moduleKey, true));

    area.removeFromTop (6.0f);
    g.setColour (p.ink);
    g.setFont (uiFont (isModule ? 22.0f : 20.0f, Weight::light));
    g.drawText (entry->title, area.removeFromTop (28.0f), juce::Justification::centredLeft, true);
    area.removeFromTop (8.0f);
    const auto headerHeight = area.getY() - top;

    // Long entries scroll inside the body rather than running into the patch sentence below.
    g.saveState();
    g.reduceClipRegion (bounds.withTrimmedTop ((int) headerHeight).expanded (4, 0));
    area.translate (0.0f, -entryScroll);
    const float contentTop = area.getY();

    if (isModule && entry->explainer.isNotEmpty())
        drawParagraph (g, {}, entry->explainer, area, p.ink2);

    drawParagraph (g, "What it does", entry->what, area, p.ink);
    drawParagraph (g, "Listen for", entry->listenFor, area, p.ink);
    drawParagraph (g, "Try this", entry->tryThis, area, p.ink);
    drawLinks (g, entry->glossaryTerms, area);
    entryMaxScroll = std::max (0.0f, area.getY() - contentTop - ((float) bounds.getHeight() - headerHeight));

    // More below: fade the last lines out so it reads as scrollable.
    if (entryScroll < entryMaxScroll - 1.0f)
    {
        const auto fade = bounds.toFloat().removeFromBottom (28.0f).expanded (4.0f, 0.0f);
        g.setGradientFill (juce::ColourGradient (p.surface.withAlpha (0.0f), fade.getX(), fade.getY(),
                                                 p.surface, fade.getX(), fade.getBottom(), false));
        g.fillRect (fade);
    }

    g.restoreState();
}

void ExplainPanel::paintGlossary (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto& p = paletteOf (*this);
    const auto& content = content::Content::get();

    g.saveState();
    g.reduceClipRegion (bounds.expanded (4, 0));

    if (glossaryTerm.isEmpty())
    {
        auto area = bounds.toFloat();
        drawCaption (g, "Glossary", area.removeFromTop (16.0f), p.ink2);
        area.removeFromTop (6.0f);

        float y = area.getY() - glossaryScroll;
        const auto font = uiFont (13.0f);

        for (const auto* term : content.glossaryInOrder())
        {
            const juce::Rectangle<float> row (area.getX(), y, area.getWidth(), 26.0f);

            if (row.getBottom() > area.getY() && row.getY() < area.getBottom())
            {
                g.setColour (p.well);
                g.fillRoundedRectangle (row.reduced (0.0f, 1.0f), 8.0f);
                g.setColour (p.ink);
                g.setFont (font);
                g.drawText (term->term, row.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
                links.push_back ({ row.toNearestInt().getIntersection (bounds), term->id, term->term });
            }

            y += 28.0f;
        }

        glossaryContentHeight = y + glossaryScroll - area.getY();
    }
    else if (const auto* term = content.glossary (glossaryTerm))
    {
        auto area = bounds.toFloat();

        const auto back = area.removeFromTop (22.0f).withWidth (74.0f);
        g.setColour (p.accentSoft);
        g.fillRoundedRectangle (back, 11.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xb9 All terms"), back, juce::Justification::centred, false);
        backArea = back.toNearestInt();

        area.removeFromTop (12.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (22.0f, Weight::light));
        g.drawText (term->term, area.removeFromTop (30.0f), juce::Justification::centredLeft, true);
        area.removeFromTop (8.0f);
        drawParagraph (g, {}, term->definition, area, p.ink);

        if (term->seeAlso.size() > 0)
        {
            area.removeFromTop (4.0f);
            auto labelled = area;
            drawCaption (g, "See also", labelled.removeFromTop (14.0f), p.ink2);
            labelled.removeFromTop (4.0f);

            const auto font = uiFont (12.0f);
            float x = labelled.getX(), y = labelled.getY();

            for (const auto& id : term->seeAlso)
            {
                const auto* other = content.glossary (id);
                if (other == nullptr)
                    continue;

                const float w = textWidth (font, other->term) + 16.0f;
                if (x + w > labelled.getRight()) { x = labelled.getX(); y += 26.0f; }

                const juce::Rectangle<float> chip (x, y, w, 22.0f);
                g.setColour (p.accentSoft);
                g.fillRoundedRectangle (chip, 11.0f);
                g.setColour (p.ink);
                g.drawText (other->term, chip, juce::Justification::centred, false);
                links.push_back ({ chip.toNearestInt(), id, other->term });
                x += w + 6.0f;
            }
        }
    }

    g.restoreState();
}

void ExplainPanel::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 18.0f, p, Elevation::raised, p.surface);

    if (collapsed)
    {
        links.clear();
        backArea = {};

        // The name, reading downwards from under the button.
        auto titleFont = uiFont (13.0f, Weight::medium);
        titleFont.setExtraKerningFactor (0.22f);
        const float length = textWidth (titleFont, "EXPLAIN");
        const auto origin = juce::Point<float> ((float) getWidth() * 0.5f, (float) unfoldButton.getBottom() + 18.0f);

        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, origin.x, origin.y));
        g.setFont (titleFont);
        g.setColour (p.ink);
        g.drawText ("EXPLAIN", juce::Rectangle<float> (origin.x, origin.y - 9.0f, length + 4.0f, 18.0f),
                    juce::Justification::centredLeft, false);
        g.restoreState();
        syncLinks();
        return;
    }

    auto titleFont = uiFont (13.0f, Weight::medium);
    titleFont.setExtraKerningFactor (0.22f);
    g.setFont (titleFont);
    g.setColour (p.ink);
    g.drawText ("EXPLAIN", headerRow, juce::Justification::centredLeft, false);

    links.clear();
    backArea = {};

    // The glossary borrows the room of the patch sentence as well.
    if (glossaryOpen)
        paintGlossary (g, bodyArea.withBottom (patchArea.getBottom()));
    else
        paintEntry (g, bodyArea);

    if (! glossaryOpen)
    {
        auto area = patchArea.toFloat();
        drawCaption (g, "Your patch", area.removeFromTop (14.0f), p.ink2);
        area.removeFromTop (4.0f);

        const auto font = uiFont (12.0f);
        g.setFont (font);
        g.setColour (p.ink);
        const auto lines = wrapLines (font, patchText, area.getWidth());
        drawLines (g, lines, area.getPosition(), area.getWidth(), lineHeight);
    }

    drawCaption (g, "Scope", juce::Rectangle<float> ((float) scopeArea.getX(), (float) scopeArea.getY() - 14.0f, 80.0f, 12.0f), p.ink2);
    syncLinks();
}

void ExplainPanel::mouseDown (const juce::MouseEvent& e)
{
    if (glossaryOpen && ! backArea.isEmpty() && backArea.contains (e.getPosition()))
    {
        showGlossary ({});
        return;
    }

    for (const auto& l : links)
        if (l.area.contains (e.getPosition()) && (glossaryOpen || bodyArea.contains (e.getPosition())))
        {
            showGlossary (l.glossaryId);
            return;
        }
}

void ExplainPanel::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (glossaryOpen && glossaryTerm.isNotEmpty())
        return;

    if (glossaryOpen)
    {
        const float visible = (float) (bodyArea.withBottom (patchArea.getBottom()).getHeight() - 22);
        glossaryScroll = juce::jlimit (0.0f, std::max (0.0f, glossaryContentHeight - visible), glossaryScroll - wheel.deltaY * 140.0f);
    }
    else
    {
        entryScroll = juce::jlimit (0.0f, entryMaxScroll, entryScroll - wheel.deltaY * 140.0f);
    }

    repaint();
}

} // namespace scratchpad::ui
