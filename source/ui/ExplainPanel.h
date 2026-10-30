#pragma once

#include "content/Content.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/IconButton.h"
#include "ui/LookAndFeel.h"
#include "ui/Scope.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace scratchpad::ui
{

/** The right-hand panel. It explains whatever the pointer or keyboard focus is on: What it does,
    Listen for, Try this, and links to the glossary terms the text uses. Below it, the patch described
    in a sentence, and the shared scope.

    Any control names itself with the component property "explainId" (a parameter ID, or a module key from
    the copy). Hovering or focusing the control, or anything inside it, shows that entry. Hovering a card's
    background shows the module, unless a module was pinned with its "?" button; pointing at a control
    always wins. The glossary lists every term and opens one at a time.

    The panel can fold away to a narrow rail (`railWidth`) that holds only the button to bring it back and its
    title. Whether it is folded is the window's decision (it follows the window size and the user's choice,
    see ViewSettings): this class draws either state and asks for a change through `onToggleRequested`.

    Keyboard and screen reader: the panel is a group. Tab visits the Glossary button, the fold button, the text
    being shown (a reading area that Up, Down, Page Up, Page Down, Home and End scroll, and that a screen reader reads
    whole), then every glossary link in it (Enter opens one; each is a real button, which the painted chips are
    not), then the patch sentence and the scope's two selectors. Escape closes the glossary. Focus landing on a
    control elsewhere still shows that control's entry, as pointing at it does, but focus on the panel's own links
    and text never replaces what is being read. */
class ExplainPanel final : public juce::Component,
                           private juce::FocusChangeListener,
                           private juce::Timer
{
public:
    explicit ExplainPanel (plugin::ScratchpadProcessor&);
    ~ExplainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    /** Shows an entry by parameter ID or module key. A pin sticks until the pointer or focus lands on a control. */
    void show (const juce::String& id, bool pin);
    void showGlossary (const juce::String& termId); // empty id: the list of all terms
    const juce::String& getShownId() const noexcept { return shownId; }
    bool isShowingGlossary() const noexcept { return glossaryOpen; }

    void setCollapsed (bool);
    bool isCollapsed() const noexcept { return collapsed; }

    /** Called when the user presses the fold or unfold button. The owner decides and calls `setCollapsed`. */
    std::function<void()> onToggleRequested;

    /** Moves keyboard focus to whichever of the fold and unfold buttons is showing. */
    void focusToggleButton();

    /** A module card's "?" button: pins `moduleKey` in the panel of the window `from` is in, unfolding the panel
        if it is folded. */
    static void pinFrom (const juce::Component& from, const juce::String& moduleKey);

    /** The ID a component stands for: its own "explainId" property, or the nearest parent's. Empty if none. */
    static juce::String explainIdOf (const juce::Component*);

    /** Where the pointer or focus is, in a form tests can drive. */
    void pointAt (const juce::Component*);

    /** Scrolls what is shown (a long entry, or the glossary list) by `pixels`, down if positive. */
    void scrollBy (float pixels);

    /** Everything a screen reader is given for what is shown: title, text and the words it links to. */
    juce::String getReadableText() const;

    static constexpr int width = 280;
    static constexpr int railWidth = 56;

private:
    struct Link
    {
        juce::Rectangle<int> area;
        juce::String glossaryId;
        juce::String label;
    };

    class Body;
    class LinkButton;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return a11y::makeGroupHandler (*this); }
    void closeGlossary();
    void openLink (int index); // -1: back to the list of terms
    void collectLinks();
    void syncLinks();
    void revealLink (int index);
    void refreshReadableText();
    juce::Rectangle<int> viewport() const;

    void globalFocusChanged (juce::Component*) override;
    void timerCallback() override;

    void paintEntry (juce::Graphics&, juce::Rectangle<int> area);
    void paintGlossary (juce::Graphics&, juce::Rectangle<int> area);
    float drawParagraph (juce::Graphics&, const juce::String& heading, const juce::String& text, juce::Rectangle<float>& area, juce::Colour ink);
    void drawLinks (juce::Graphics&, const juce::StringArray& terms, juce::Rectangle<float>& area);
    juce::Colour familyOf (const juce::String& moduleKey, bool ink) const;

    plugin::ScratchpadProcessor& processor;
    Scope scope;
    juce::TextButton glossaryButton { "Glossary" };
    IconButton foldButton { "Fold Explain panel", "M6 3l5 5-5 5" };
    IconButton unfoldButton { "Show Explain panel", "M10 3L5 8l5 5" };
    std::unique_ptr<Body> body;
    std::vector<std::unique_ptr<LinkButton>> linkButtons;
    a11y::Region patchRegion;
    bool collapsed = false;

    juce::String shownId = "osc1";
    bool pinned = false;
    bool glossaryOpen = false;
    juce::String glossaryTerm; // empty: the list
    float glossaryScroll = 0.0f;
    float glossaryContentHeight = 0.0f;
    float entryScroll = 0.0f, entryMaxScroll = 0.0f;
    juce::String patchText;

    std::vector<Link> links;
    juce::Rectangle<int> headerRow, bodyArea, patchArea, scopeArea, backArea;
};

} // namespace scratchpad::ui
