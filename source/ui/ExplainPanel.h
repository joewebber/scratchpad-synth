#pragma once

#include "content/Content.h"
#include "plugin/PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/Scope.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace scratchpad::ui
{

/** The right-hand panel. It explains whatever the pointer or keyboard focus is on: What it does,
    Listen for, Try this, and links to the glossary terms the text uses. Below it, the patch described
    in a sentence, and the shared scope.

    Any control names itself with the component property "explainId" (a parameter ID, or a module key from
    the copy). Hovering or focusing the control, or anything inside it, shows that entry. Hovering a card's
    background shows the module, unless a module was pinned with its "?" button; pointing at a control
    always wins. The glossary lists every term and opens one at a time. */
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
    void mouseMove (const juce::MouseEvent&) override; // also hears every move in the window: see the constructor
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void visibilityChanged() override;

    /** Shows an entry by parameter ID or module key. A pin sticks until the pointer or focus lands on a control. */
    void show (const juce::String& id, bool pin);
    void showGlossary (const juce::String& termId); // empty id: the list of all terms
    const juce::String& getShownId() const noexcept { return shownId; }
    bool isShowingGlossary() const noexcept { return glossaryOpen; }

    /** The ID a component stands for: its own "explainId" property, or the nearest parent's. Empty if none. */
    static juce::String explainIdOf (const juce::Component*);

    /** Where the pointer or focus is, in a form tests can drive. */
    void pointAt (const juce::Component*);

    static constexpr int width = 280;

private:
    struct Link
    {
        juce::Rectangle<int> area;
        juce::String glossaryId;
    };

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

    juce::String shownId = "osc1";
    bool pinned = false;
    bool glossaryOpen = false;
    juce::String glossaryTerm; // empty: the list
    float glossaryScroll = 0.0f;
    float glossaryContentHeight = 0.0f;
    juce::String patchText;

    std::vector<Link> links;
    juce::Rectangle<int> headerRow, bodyArea, patchArea, scopeArea, backArea;
};

} // namespace scratchpad::ui
