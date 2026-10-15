// Throwaway: renders the editor to PNG files for visual checks.
// usage: ScratchpadSnapshot <name> [explainId] [--note] [--tap N] [--spectrum] [--pin] [--glossary [term]]
#include "plugin/PluginProcessor.h"
#include "ui/ExplainPanel.h"
#include <juce_events/juce_events.h>
using namespace scratchpad;
using plugin::Module;

static juce::Component* find (juce::Component& c, const juce::String& id) { return c.findChildWithID (id); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    plugin::ScratchpadProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    juce::String name = argc > 1 ? argv[1] : "snap", explainId;
    bool note = false, spectrum = false, pin = false, glossary = false;
    int tap = 2;
    juce::String glossaryTerm;
    for (int i = 2; i < argc; ++i)
    {
        juce::String a = argv[i];
        if (a == "--note") note = true;
        else if (a == "--spectrum") spectrum = true;
        else if (a == "--pin") pin = true;
        else if (a == "--tap") tap = juce::String (argv[++i]).getIntValue();
        else if (a == "--glossary") { glossary = true; if (i + 1 < argc && juce::String (argv[i + 1]).substring (0, 2) != "--") glossaryTerm = argv[++i]; }
        else explainId = a;
    }

    p.pickWave (2);
    for (auto m : { Module::osc2, Module::filter, Module::ampEnv, Module::filterEnv, Module::lfo1 })
        p.addModule (m);
    p.connectMod (engine::ModSource::lfo1, engine::ModDest::cutoff);
    p.connectMod (engine::ModSource::lfo1, engine::ModDest::pitchAll);
    p.setScopeTap ((engine::ScopeTap) tap);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    ed->setSize (1500, 840);

    auto* panel = dynamic_cast<ui::ExplainPanel*> (find (*ed, "explainPanel"));

    // The scope's tap and mode are the panel's own controls: click them.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

    if (note)
    {
        plugin::UiEvent e; e.type = plugin::UiEvent::Type::noteOn; e.note = 57; e.value = 0.8f;
        p.getUiEvents().push (e);
    }

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int round = 0; round < 12; ++round)
    {
        for (int b = 0; b < 8; ++b) { p.processBlock (buffer, midi); midi.clear(); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    if (panel != nullptr)
    {
        if (explainId.isNotEmpty()) panel->show (explainId, pin);
        if (glossary) panel->showGlossary (glossaryTerm);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File out ("/tmp/snap_" + name + ".png");
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (img, stream);
    (void) spectrum;
    ed.reset();
    return 0;
}
