#include "content/Content.h"
#include "plugin/PluginEditor.h"
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

TEST_CASE ("Every control copy entry is complete and links to real glossary terms", "[content]")
{
    const auto& content = content::Content::get();
    REQUIRE (! content.controlIds().isEmpty());

    for (const auto& id : content.controlIds())
    {
        INFO (id.toStdString());
        const auto* entry = content.control (id);
        REQUIRE (entry->title.isNotEmpty());
        REQUIRE (entry->what.isNotEmpty());
        REQUIRE (entry->listenFor.isNotEmpty());
        REQUIRE (entry->tryThis.isNotEmpty());

        for (const auto& term : entry->glossaryTerms)
            REQUIRE (content.glossary (term) != nullptr);
    }
}

TEST_CASE ("The M5 modules, controls and glossary terms have copy", "[content]")
{
    const auto& content = content::Content::get();

    for (const char* key : { "distortion", "chorus", "delay", "reverb" })
    {
        INFO (key);
        const auto* m = content.module (key);
        REQUIRE (m != nullptr);
        REQUIRE (! m->glossaryTerms.isEmpty());
        REQUIRE (content.param (juce::String (key) + ".bypass") != nullptr);
    }

    for (const char* id : { "control.limiter", "control.effectGrip", "control.oscMore" })
    {
        INFO (id);
        REQUIRE (content.control (id) != nullptr);
    }

    for (const char* term : { "hard-sync", "ring-modulation", "fm", "unison", "anti-aliasing", "oversampling", "limiter",
                              "tempo-sync", "bpm", "ping-pong", "wet-dry", "feedback", "damping" })
    {
        INFO (term);
        REQUIRE (content.glossary (term) != nullptr);
    }
}

namespace
{
    void collect (juce::Component& c, std::vector<juce::Component*>& out)
    {
        out.push_back (&c);

        for (auto* child : c.getChildren())
            collect (*child, out);
    }
}

TEST_CASE ("Every control in the editor explains itself", "[content][ui]")
{
    const auto& content = content::Content::get();

    plugin::ScratchpadProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    REQUIRE (editor != nullptr);

    std::vector<juce::Component*> all;
    collect (*editor, all);

    int checked = 0;

    for (auto* c : all)
    {
        const bool interactive = dynamic_cast<juce::Button*> (c) != nullptr
                              || dynamic_cast<juce::Slider*> (c) != nullptr
                              || dynamic_cast<juce::MidiKeyboardComponent*> (c) != nullptr;

        // The glossary button explains itself in its own wording.
        if (! interactive || c->getComponentID() == "explainPanel" || c->getTitle() == "Glossary")
            continue;

        const auto id = ui::ExplainPanel::explainIdOf (c);
        INFO (c->getTitle().toStdString());
        REQUIRE (id.isNotEmpty());
        REQUIRE ((content.param (id) != nullptr || content.control (id) != nullptr || content.module (id) != nullptr));
        ++checked;
    }

    REQUIRE (checked > 20);
}
