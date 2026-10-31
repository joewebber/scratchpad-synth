#include "ui/MorePopover.h"

#include "plugin/Parameters.h"

#include <cmath>

namespace scratchpad::ui
{

namespace pop = layout::popover;

//==============================================================================
MoreButton::MoreButton (const juce::String& moduleName)
    : juce::Button (moduleName + " more options"), name (moduleName)
{
    setPaintingIsUnclipped (true);
    setClickingTogglesState (false);
    setToggleable (true);
    setTitle (moduleName + " more options");
    getProperties().set ("explainId", "control.more");
    refreshTitle();
}

void MoreButton::refreshTitle()
{
    // The title stays put: "pressed" (open) is the toggle state, and what the dot means is in the description.
    setToggleState (open, juce::dontSendNotification);
    setDescription ("Opens the " + name + "'s other controls."
                    + (marker == Marker::warning ? " Something in there needs a look." : marker == Marker::inUse ? " Some of them are in use." : juce::String()));
}

void MoreButton::setOpen (bool shouldBeOpen)
{
    if (open != shouldBeOpen)
    {
        open = shouldBeOpen;
        refreshTitle();
        repaint();
    }
}

void MoreButton::setMarker (Marker newMarker)
{
    if (marker != newMarker)
    {
        marker = newMarker;
        refreshTitle();
        repaint();
    }
}

void MoreButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    auto fill = open ? p.ink : p.surface;
    if (! open && down)
        fill = fill.interpolatedWith (p.well, 0.5f);
    else if (! open && highlighted)
        fill = fill.interpolatedWith (p.ink, 0.06f);

    drawRaised (g, bounds, 7.0f, p, Elevation::small, fill);

    g.setColour (open ? p.surface : p.ink2);
    const auto c = bounds.getCentre();
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ c.x + (float) i * 4.5f, c.y }));

    if (marker != Marker::none)
    {
        const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ bounds.getRight() - 1.5f, bounds.getY() + 1.5f });
        g.setColour (p.surface);
        g.fillEllipse (dot.expanded (1.5f));
        g.setColour (marker == Marker::warning ? p.accentInk : p.srcInk);
        g.fillEllipse (dot);
    }

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds, 7.0f, p.ink);
}

namespace
{
    /** The Remove button's look: a raised 30 px pill, a cross and the label, left aligned. */
    class RemoveButton final : public juce::Button
    {
    public:
        explicit RemoveButton (const juce::String& words) : juce::Button (words), label (words) { setPaintingIsUnclipped (true); }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto& p = paletteOf (*this);
            const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            auto fill = p.surface;

            if (down)
                fill = fill.interpolatedWith (p.well, 0.5f);
            else if (highlighted)
                fill = fill.interpolatedWith (p.ink, 0.06f);

            drawRaised (g, bounds, 9.0f, p, Elevation::small, fill);

            juce::Path cross;
            const auto c = juce::Point<float> (bounds.getX() + 15.0f, bounds.getCentreY());
            cross.startNewSubPath (c.x - 3.5f, c.y - 3.5f);
            cross.lineTo (c.x + 3.5f, c.y + 3.5f);
            cross.startNewSubPath (c.x + 3.5f, c.y - 3.5f);
            cross.lineTo (c.x - 3.5f, c.y + 3.5f);
            g.setColour (p.ink2);
            g.strokePath (cross, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setFont (uiFont (13.0f));
            g.drawText (label, bounds.withTrimmedLeft (30.0f), juce::Justification::centredLeft, true);

            if (hasKeyboardFocus (false))
                ScratchpadLookAndFeel::drawFocusRing (g, bounds, 9.0f, p.ink);
        }

    private:
        juce::String label;
    };
}

//==============================================================================
/** A row: a hairline, then its content. Subclasses say how tall the content is and place their controls. */
class MorePopover::Row : public juce::Component
{
public:
    static constexpr int ruleSpace = 11; // the hairline and the 10 px under it

    int totalHeight() const { return ruleSpace + contentHeight(); }

    virtual int contentHeight() const = 0;
    virtual void layoutContent (juce::Rectangle<int> content) = 0;
    virtual juce::Component* find (const juce::String&) const { return nullptr; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (paletteOf (*this).line);
        g.fillRect (0, 0, getWidth(), 1);
    }

    void resized() override { layoutContent (getLocalBounds().withTrimmedTop (ruleSpace)); }

protected:
    static int noteHeight (const juce::String& text, int width)
    {
        if (text.isEmpty())
            return 0;

        juce::AttributedString s;
        s.append (text, uiFont (12.0f));
        juce::TextLayout l;
        l.createLayout (s, (float) width);
        return 16 * juce::jmax (1, (int) std::round (l.getHeight() / 14.0f));
    }

    static void drawNote (juce::Graphics& g, const Palette& p, const juce::String& text, juce::Rectangle<int> area)
    {
        g.setColour (p.mute);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (text, area, juce::Justification::topLeft, 4, 1.0f);
    }

    int width() const { return pop::width - 2 * pop::padding; }
};

class MorePopover::SwitchRow final : public Row
{
public:
    SwitchRow (plugin::ScratchpadProcessor& p, const juce::String& paramId, const juce::String& labelText,
               const juce::String& accessibleName, const juce::String& noteText, juce::Colour Palette::* familyColour)
        : label (labelText), note (noteText), family (familyColour), toggle (*this), id (paramId)
    {
        auto* param = p.apvts.getParameter (paramId);
        jassert (param != nullptr);

        toggle.setTitle (accessibleName);
        toggle.setClickingTogglesState (true);
        toggle.setToggleable (true);

        if (const auto* entry = content::Content::get().param (paramId))
            toggle.setDescription (entry->what);

        toggle.getProperties().set ("explainId", paramId);
        attachment = std::make_unique<juce::ButtonParameterAttachment> (*param, toggle, &p.undoManager);
        toggle.onClick = [this, &p] { p.undoManager.setCurrentTransactionName (toggle.getTitle() + (toggle.getToggleState() ? " on" : " off")); };
        addAndMakeVisible (toggle);
    }

    int contentHeight() const override { return 18 + (note.isEmpty() ? 0 : 7 + noteHeight (note, width())); }

    void layoutContent (juce::Rectangle<int> c) override
    {
        toggle.setBounds (c.withHeight (18));
        noteArea = c.withTrimmedTop (25);
    }

    juce::Component* find (const juce::String& paramId) const override { return paramId == id ? const_cast<Toggle*> (&toggle) : nullptr; }

    void paint (juce::Graphics& g) override
    {
        Row::paint (g);

        if (note.isNotEmpty())
            drawNote (g, paletteOf (*this), note, noteArea);
    }

private:
    /** The row's label on the left, the 34 x 18 track on the right. */
    class Toggle final : public juce::Button
    {
    public:
        explicit Toggle (SwitchRow& r) : juce::Button ({}), owner (r) { setPaintingIsUnclipped (true); }

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const auto& p = paletteOf (*this);
            const auto bounds = getLocalBounds().toFloat();
            const bool on = getToggleState();

            g.setColour (highlighted ? p.ink : p.ink);
            g.setFont (uiFont (13.0f));
            g.drawText (owner.label, bounds.withTrimmedRight (44.0f), juce::Justification::centredLeft, true);

            const auto track = juce::Rectangle<float> (34.0f, 18.0f).withRightX (bounds.getRight()).withCentre ({ bounds.getRight() - 17.0f, bounds.getCentreY() });
            const auto fam = p.*(owner.family);

            if (on)
            {
                g.setColour (fam);
                g.fillRoundedRectangle (track, 9.0f);
            }
            else
                drawInset (g, track, 9.0f, p, p.well);

            const float x = on ? track.getRight() - 9.0f - 2.0f + 0.0f : track.getX() + 9.0f + 2.0f;
            drawRaised (g, juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ x - 0.0f, track.getCentreY() }), 7.0f, p, Elevation::small, p.knob);

            if (hasKeyboardFocus (false))
                ScratchpadLookAndFeel::drawFocusRing (g, bounds, 6.0f, p.ink);
        }

    private:
        SwitchRow& owner;
    };

    juce::String label, note;
    juce::Colour Palette::* family;
    Toggle toggle;
    juce::String id;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
    juce::Rectangle<int> noteArea;
};

class MorePopover::SegmentsRow final : public Row
{
public:
    SegmentsRow (plugin::ScratchpadProcessor& p, const juce::String& paramId, const juce::String& labelText,
                 const juce::String& accessibleName, int cols, const juce::StringArray& words, juce::Colour Palette::* family)
        : label (labelText), id (paramId), segments (p.apvts, paramId, accessibleName, p.undoManager, family)
    {
        columns = cols;
        segments.setColumns (cols);

        if (! words.isEmpty())
            segments.setDisplayLabels (words);

        addAndMakeVisible (segments);
    }

    static constexpr int captionHeight = 12, captionGap = 7;

    int contentHeight() const override { return captionHeight + captionGap + segmentsHeight(); }

    void layoutContent (juce::Rectangle<int> c) override
    {
        c.removeFromTop (captionHeight + captionGap);
        segments.setBounds (c.withHeight (segmentsHeight()));
    }

    juce::Component* find (const juce::String& paramId) const override { return paramId == id ? const_cast<ChoiceSegments*> (&segments) : nullptr; }

    void paint (juce::Graphics& g) override
    {
        Row::paint (g);
        drawCaption (g, label, juce::Rectangle<float> (0.0f, (float) ruleSpace, (float) getWidth(), (float) captionHeight), paletteOf (*this).ink2);
    }

private:
    int segmentsHeight() const { const int rows = ChoiceSegments::rowsFor (segments.getNumSegments(), columns); return 4 + rows * 22 + (rows - 1) * 2; }

    juce::String label, id;
    int columns = 0;
    ChoiceSegments segments;
};

class MorePopover::OffChoiceRow final : public Row
{
public:
    OffChoiceRow (plugin::ScratchpadProcessor& p, const juce::String& enabledId, const juce::String& modeId, const juce::String& labelText,
                  const juce::String& accessibleName, int cols, const juce::StringArray& words, juce::Colour Palette::* family)
        : label (labelText), columns (cols), segments (words, enabledId, accessibleName, p.undoManager, family),
          enabled (*p.apvts.getParameter (enabledId)), mode (*p.apvts.getParameter (modeId)), undo (p.undoManager),
          enabledWatcher (enabled, [this] (float) { follow(); }), modeWatcher (mode, [this] (float) { follow(); })
    {
        segments.setColumns (cols);
        segments.onPick = [this, accessibleName] (int i)
        {
            undo.beginNewTransaction (accessibleName + " " + segmentsLabel (i));

            const auto set = [] (juce::RangedAudioParameter& param, float value)
            {
                param.beginChangeGesture();
                param.setValueNotifyingHost (param.convertTo0to1 (value));
                param.endChangeGesture();
            };

            set (enabled, i > 0 ? 1.0f : 0.0f);

            if (i > 0)
                set (mode, (float) (i - 1));
        };

        addAndMakeVisible (segments);
        follow();
    }

    static constexpr int captionHeight = 12, captionGap = 7;

    int contentHeight() const override { return captionHeight + captionGap + segmentsHeight(); }

    void layoutContent (juce::Rectangle<int> c) override
    {
        c.removeFromTop (captionHeight + captionGap);
        segments.setBounds (c.withHeight (segmentsHeight()));
    }

    juce::Component* find (const juce::String& paramId) const override
    {
        return paramId == enabled.getParameterID() || paramId == mode.getParameterID() ? const_cast<ChoiceSegments*> (&segments) : nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        Row::paint (g);
        drawCaption (g, label, juce::Rectangle<float> (0.0f, (float) ruleSpace, (float) getWidth(), (float) captionHeight), paletteOf (*this).ink2);
    }

private:
    int segmentsHeight() const { const int rows = ChoiceSegments::rowsFor (segments.getNumSegments(), columns); return 4 + rows * 22 + (rows - 1) * 2; }

    juce::String segmentsLabel (int i) const { return i == 0 ? "off" : mode.getText (mode.convertTo0to1 ((float) (i - 1)), 0); }

    void follow()
    {
        const bool on = enabled.convertFrom0to1 (enabled.getValue()) > 0.5f;
        segments.setSelectedIndex (on ? 1 + (int) std::lround (mode.convertFrom0to1 (mode.getValue())) : 0);
    }

    juce::String label;
    int columns = 0;
    ChoiceSegments segments;
    juce::RangedAudioParameter &enabled, &mode;
    juce::UndoManager& undo;
    juce::ParameterAttachment enabledWatcher, modeWatcher;
};

class MorePopover::KnobsRow final : public Row
{
public:
    KnobsRow (plugin::ScratchpadProcessor& p, const juce::String& labelText, const std::vector<KnobSpec>& specs, const juce::String& noteText,
              juce::Colour Palette::* familyInk)
        : label (labelText), note (noteText), ink (familyInk)
    {
        for (const auto& s : specs)
        {
            auto k = std::make_unique<Knob> (p.apvts, s.paramId, s.caption, p.undoManager,
                                             s.accessibleName.isEmpty() ? s.caption : s.accessibleName, layout::knobs::popover);
            k->setShowsCaptionAndValue();

            if (s.setup)
                s.setup (*k);

            ids.add (s.paramId);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }

        applyColours();
    }

    static constexpr int captionHeight = 12, captionGap = 7, knobGap = 14;

    int contentHeight() const override
    {
        return captionHeight + captionGap + knobs.front()->height() + (note.isEmpty() ? 0 : 7 + noteHeight (note, width()));
    }

    void layoutContent (juce::Rectangle<int> c) override
    {
        c.removeFromTop (captionHeight + captionGap);
        int x = c.getX();

        for (auto& k : knobs)
        {
            k->setBounds (x, c.getY(), k->getSize().cell, k->height());
            x += k->getSize().cell + knobGap;
        }

        noteArea = c.withTrimmedTop (knobs.front()->height() + 7);
    }

    juce::Component* find (const juce::String& paramId) const override
    {
        const int i = ids.indexOf (paramId);
        return i >= 0 ? knobs[(size_t) i].get() : nullptr;
    }

    void lookAndFeelChanged() override { applyColours(); }

    void paint (juce::Graphics& g) override
    {
        Row::paint (g);
        drawCaption (g, label, juce::Rectangle<float> (0.0f, (float) ruleSpace, (float) getWidth(), (float) captionHeight), paletteOf (*this).ink2);

        if (note.isNotEmpty())
            drawNote (g, paletteOf (*this), note, noteArea);
    }

private:
    void applyColours()
    {
        for (auto& k : knobs)
            k->setFamilyColour (paletteOf (*this).*ink);
    }

    juce::String label, note;
    juce::Colour Palette::* ink;
    juce::StringArray ids;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::Rectangle<int> noteArea;
};

class MorePopover::NoteRow final : public Row
{
public:
    explicit NoteRow (const juce::String& t) : text (t) {}

    int contentHeight() const override { return noteHeight (text, width()); }
    void layoutContent (juce::Rectangle<int> c) override { area = c; }

    void paint (juce::Graphics& g) override
    {
        Row::paint (g);
        drawNote (g, paletteOf (*this), text, area);
    }

private:
    juce::String text;
    juce::Rectangle<int> area;
};

class MorePopover::RemoveRow final : public Row
{
public:
    RemoveRow (const juce::String& labelText, const juce::String& accessibleName, std::function<void()> onPress) : button (labelText)
    {
        button.setTitle (accessibleName);
        button.getProperties().set ("explainId", "control.remove");
        button.setDescription ("Takes this module out of the patch and puts its settings back to their defaults. You can undo it.");
        button.onClick = std::move (onPress);
        addAndMakeVisible (button);
    }

    int contentHeight() const override { return pop::removeHeight; }
    void layoutContent (juce::Rectangle<int> c) override { button.setBounds (c.withHeight (pop::removeHeight)); }

private:
    RemoveButton button;
};

//==============================================================================
//==============================================================================
MorePopover::MorePopover (const juce::String& moduleTitle, juce::Colour Palette::* familyColour)
    : title (moduleTitle), family (familyColour)
{
    setTitle (moduleTitle + " options");
    setFocusContainerType (FocusContainerType::focusContainer);

    class CloseButton final : public juce::Button
    {
    public:
        CloseButton() : juce::Button ("Close") { setTitle ("Close"); setPaintingIsUnclipped (true); }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto& p = paletteOf (*this);
            const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
            auto fill = p.surface;

            if (down)
                fill = fill.interpolatedWith (p.well, 0.5f);
            else if (highlighted)
                fill = fill.interpolatedWith (p.ink, 0.06f);

            drawRaised (g, bounds, bounds.getWidth() * 0.5f, p, Elevation::small, fill);

            juce::Path cross;
            const auto c = bounds.getCentre();
            cross.startNewSubPath (c.x - 3.0f, c.y - 3.0f);
            cross.lineTo (c.x + 3.0f, c.y + 3.0f);
            cross.startNewSubPath (c.x + 3.0f, c.y - 3.0f);
            cross.lineTo (c.x - 3.0f, c.y + 3.0f);
            g.setColour (p.ink);
            g.strokePath (cross, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (hasKeyboardFocus (false))
                ScratchpadLookAndFeel::drawFocusRing (g, bounds, bounds.getWidth() * 0.5f, p.ink);
        }
    };

    closeButton = std::make_unique<CloseButton>();
    closeButton->onClick = [this] { close(); };
    addAndMakeVisible (*closeButton);
}

MorePopover::~MorePopover()
{
    if (onClosed)
        onClosed();
}

void MorePopover::addRow (std::unique_ptr<Row> row)
{
    addAndMakeVisible (*row);
    rows.push_back (std::move (row));
}

void MorePopover::addSwitch (plugin::ScratchpadProcessor& p, const juce::String& paramId, const juce::String& label,
                             const juce::String& accessibleName, const juce::String& note)
{
    addRow (std::make_unique<SwitchRow> (p, paramId, label, accessibleName, note, family));
}

void MorePopover::addSegments (plugin::ScratchpadProcessor& p, const juce::String& paramId, const juce::String& label,
                               const juce::String& accessibleName, int columns, const juce::StringArray& displayWords)
{
    addRow (std::make_unique<SegmentsRow> (p, paramId, label, accessibleName, columns, displayWords, family));
}

void MorePopover::addOffChoice (plugin::ScratchpadProcessor& p, const juce::String& enabledId, const juce::String& modeId,
                                const juce::String& label, const juce::String& accessibleName, int columns, const juce::StringArray& words)
{
    addRow (std::make_unique<OffChoiceRow> (p, enabledId, modeId, label, accessibleName, columns, words, family));
}

void MorePopover::addKnobs (plugin::ScratchpadProcessor& p, const juce::String& label, const std::vector<KnobSpec>& specs, const juce::String& note)
{
    // Pop-over knobs take the family's text colour from the family fill's matching ink.
    juce::Colour Palette::* ink = family == &Palette::src ? &Palette::srcInk : family == &Palette::flt ? &Palette::fltInk
                                : family == &Palette::amp ? &Palette::ampInk : family == &Palette::mod ? &Palette::modInk
                                : family == &Palette::fx ? &Palette::fxInk : &Palette::perfInk;
    addRow (std::make_unique<KnobsRow> (p, label, specs, note, ink));
}

void MorePopover::addNote (const juce::String& text)
{
    addRow (std::make_unique<NoteRow> (text));
}

void MorePopover::addRemove (const juce::String& label, const juce::String& accessibleName, std::function<void()> onRemove)
{
    addRow (std::make_unique<RemoveRow> (label, accessibleName, [this, remove = std::move (onRemove)]
                                         {
                                             close();
                                             remove();
                                         }));
}

juce::Component* MorePopover::findControl (const juce::String& paramId) const
{
    for (const auto& r : rows)
        if (auto* c = r->find (paramId))
            return c;

    return nullptr;
}

int MorePopover::heightForRows() const
{
    int h = 2 * pop::padding + pop::headerHeight;

    for (const auto& r : rows)
        h += pop::gap + r->totalHeight();

    return h;
}

void MorePopover::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    g.setColour (p.*family);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ (float) pop::padding + 5.0f, (float) pop::padding + pop::headerHeight * 0.5f }));

    g.setColour (p.ink);
    g.setFont (uiFont (15.0f, Weight::medium));
    const float titleWidth = textWidth (uiFont (15.0f, Weight::medium), title);
    g.drawText (title, juce::Rectangle<float> ((float) pop::padding + 18.0f, (float) pop::padding, titleWidth + 4.0f, (float) pop::headerHeight),
                juce::Justification::centredLeft, false);

    drawCaption (g, "More", juce::Rectangle<float> ((float) pop::padding + 18.0f + titleWidth + 12.0f, (float) pop::padding, 60.0f, (float) pop::headerHeight),
                 p.mute, juce::Justification::centredLeft, 0.1f);
}

void MorePopover::resized()
{
    auto area = getLocalBounds().reduced (pop::padding);
    auto header = area.removeFromTop (pop::headerHeight);
    closeButton->setBounds (header.removeFromRight (22).withSizeKeepingCentre (22, 22));

    for (auto& r : rows)
    {
        area.removeFromTop (pop::gap);
        r->setBounds (area.removeFromTop (r->totalHeight()));
    }
}

void MorePopover::close()
{
    if (box != nullptr)
        box->dismiss();
}

void MorePopover::show (std::unique_ptr<MorePopover> popover, juce::Component& anchor)
{
    popover->setSize (pop::width, popover->heightForRows());

    // The stage is the editor's scaled child; a pop-over inside it scales with everything else.
    juce::Component* top = anchor.getTopLevelComponent();
    juce::Component* stage = &anchor;

    while (stage->getParentComponent() != nullptr && stage->getParentComponent() != top)
        stage = stage->getParentComponent();

    auto* owner = popover.get();
    const auto area = stage->getLocalArea (&anchor, anchor.getLocalBounds());
    juce::Component::SafePointer<juce::Component> safeAnchor (&anchor);

    owner->onClosed = [safeAnchor]
    {
        if (safeAnchor != nullptr && safeAnchor->isShowing())
            safeAnchor->grabKeyboardFocus();
    };

    auto& callOut = juce::CallOutBox::launchAsynchronously (std::move (popover), area, stage);
    callOut.setArrowSize (0.0f);
    owner->box = &callOut;
}

} // namespace scratchpad::ui
