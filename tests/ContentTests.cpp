#include "content/Content.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace scratchpad;

TEST_CASE ("Every parameter has copy: what, listen for and try this", "[content]")
{
    const auto& content = content::Content::get();
    REQUIRE (content.loadError().isEmpty());

    plugin::ScratchpadProcessor p;

    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);

        INFO (ranged->paramID.toStdString());
        const auto* entry = content.param (ranged->paramID);
        REQUIRE (entry != nullptr);
        REQUIRE (entry->what.isNotEmpty());
        REQUIRE (entry->listenFor.isNotEmpty());
        REQUIRE (entry->tryThis.isNotEmpty());
        REQUIRE (content.module (entry->module) != nullptr);
    }
}

TEST_CASE ("The copy has no entries for parameters that do not exist", "[content]")
{
    plugin::ScratchpadProcessor p;

    for (const auto& id : content::Content::get().paramIds())
    {
        INFO (id.toStdString());
        REQUIRE (p.apvts.getParameter (id) != nullptr);
    }
}

TEST_CASE ("Modules have a concept paragraph and every glossary link resolves", "[content]")
{
    const auto& content = content::Content::get();

    for (const auto& key : content.moduleKeys())
    {
        INFO (key.toStdString());
        const auto* m = content.module (key);
        REQUIRE (m->title.isNotEmpty());
        REQUIRE (m->explainer.isNotEmpty());
        REQUIRE (m->what.isNotEmpty());

        for (const auto& term : m->glossaryTerms)
            REQUIRE (content.glossary (term) != nullptr);
    }

    for (const auto& id : content.paramIds())
        for (const auto& term : content.param (id)->glossaryTerms)
        {
            INFO (id.toStdString() << " -> " << term.toStdString());
            REQUIRE (content.glossary (term) != nullptr);
        }

    for (const auto& id : content.glossaryIds())
    {
        const auto* g = content.glossary (id);
        REQUIRE (g->definition.isNotEmpty());

        for (const auto& other : g->seeAlso)
            REQUIRE (content.glossary (other) != nullptr);
    }
}
