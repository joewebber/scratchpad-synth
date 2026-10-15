#include "content/Content.h"

#include "ContentData.h"

namespace scratchpad::content
{

namespace
{
    juce::StringArray stringsOf (const juce::var& v)
    {
        juce::StringArray out;

        if (auto* array = v.getArray())
            for (const auto& s : *array)
                out.add (s.toString());

        return out;
    }

    Entry entryOf (const juce::var& v)
    {
        Entry e;
        e.title = v.getProperty ("title", {}).toString();
        e.module = v.getProperty ("module", {}).toString();
        e.explainer = v.getProperty ("concept", {}).toString();
        e.what = v.getProperty ("what", {}).toString();
        e.listenFor = v.getProperty ("listenFor", {}).toString();
        e.tryThis = v.getProperty ("tryThis", {}).toString();
        e.glossaryTerms = stringsOf (v.getProperty ("glossaryTerms", {}));
        return e;
    }
}

const Content& Content::get()
{
    static const Content instance;
    return instance;
}

Content::Content()
{
    const auto explanations = juce::JSON::parse (juce::String::fromUTF8 (contentdata::explanations_json, contentdata::explanations_jsonSize));
    const auto glossaryJson = juce::JSON::parse (juce::String::fromUTF8 (contentdata::glossary_json, contentdata::glossary_jsonSize));

    if (auto* object = explanations.getProperty ("params", {}).getDynamicObject())
        for (const auto& p : object->getProperties())
            params[p.name.toString()] = entryOf (p.value);

    if (auto* object = explanations.getProperty ("modules", {}).getDynamicObject())
        for (const auto& p : object->getProperties())
            modules[p.name.toString()] = entryOf (p.value);

    if (auto* object = glossaryJson.getDynamicObject())
    {
        for (const auto& p : object->getProperties())
        {
            GlossaryEntry g;
            g.id = p.name.toString();
            g.term = p.value.getProperty ("term", {}).toString();
            g.definition = p.value.getProperty ("definition", {}).toString();
            g.seeAlso = stringsOf (p.value.getProperty ("seeAlso", {}));
            glossaryEntries[g.id] = g;
        }
    }

    for (const auto& [id, g] : glossaryEntries)
        ordered.push_back (&g);

    std::sort (ordered.begin(), ordered.end(), [] (const GlossaryEntry* a, const GlossaryEntry* b) { return a->term.compareIgnoreCase (b->term) < 0; });

    if (params.empty() || modules.empty() || glossaryEntries.empty())
        error = "explanations.json or glossary.json is missing or malformed";
}

const Entry* Content::param (const juce::String& id) const
{
    const auto it = params.find (id);
    return it == params.end() ? nullptr : &it->second;
}

const Entry* Content::module (const juce::String& key) const
{
    const auto it = modules.find (key);
    return it == modules.end() ? nullptr : &it->second;
}

const GlossaryEntry* Content::glossary (const juce::String& id) const
{
    const auto it = glossaryEntries.find (id);
    return it == glossaryEntries.end() ? nullptr : &it->second;
}

juce::StringArray Content::paramIds() const
{
    juce::StringArray out;
    for (const auto& [id, e] : params)
        out.add (id);
    return out;
}

juce::StringArray Content::moduleKeys() const
{
    juce::StringArray out;
    for (const auto& [id, e] : modules)
        out.add (id);
    return out;
}

juce::StringArray Content::glossaryIds() const
{
    juce::StringArray out;
    for (const auto& [id, g] : glossaryEntries)
        out.add (id);
    return out;
}

} // namespace scratchpad::content
