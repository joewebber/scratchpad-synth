#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace scratchpad::content
{

/** One explanation: the three parts the Explain panel shows. For a module, `concept` is the
    one-paragraph "what is this, really" and `title` its name. */
struct Entry
{
    juce::String title, module, explainer, what, listenFor, tryThis;
    juce::StringArray glossaryTerms;
};

struct GlossaryEntry
{
    juce::String id, term, definition;
    juce::StringArray seeAlso;
};

/** The copy: `explanations.json` and `glossary.json`, embedded in the binary so wording can change
    without touching code. Loaded once. Message thread. */
class Content
{
public:
    static const Content& get();

    const Entry* param (const juce::String& parameterId) const;
    const Entry* module (const juce::String& moduleKey) const;

    /** Copy for controls that are not parameters: bypass, solo, the keyboard, the wheels and so on. */
    const Entry* control (const juce::String& controlId) const;
    const GlossaryEntry* glossary (const juce::String& id) const;

    /** Every glossary entry, alphabetical by term. */
    const std::vector<const GlossaryEntry*>& glossaryInOrder() const noexcept { return ordered; }

    juce::StringArray paramIds() const;
    juce::StringArray moduleKeys() const;
    juce::StringArray controlIds() const;
    juce::StringArray glossaryIds() const;

    /** Empty when both files parsed; otherwise what was wrong. */
    const juce::String& loadError() const noexcept { return error; }

private:
    Content();

    std::map<juce::String, Entry> params, modules, controls;
    std::map<juce::String, GlossaryEntry> glossaryEntries;
    std::vector<const GlossaryEntry*> ordered;
    juce::String error;
};

} // namespace scratchpad::content
