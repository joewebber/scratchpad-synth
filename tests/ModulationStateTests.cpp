#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace scratchpad;
using engine::ModDest;
using engine::ModSource;
using plugin::Module;
using plugin::ScratchpadProcessor;
using Result = ScratchpadProcessor::ConnectResult;

namespace
{
    float raw (ScratchpadProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }

    void set (ScratchpadProcessor& p, const char* id, float unnormalised)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (unnormalised));
    }

    /** Osc 1 present, the modules a connection needs, nothing connected. */
    void build (ScratchpadProcessor& p)
    {
        p.addModule (Module::lfo1);
        p.addModule (Module::filter);
        p.addModule (Module::osc2);
    }

    int activeSlots (ScratchpadProcessor& p)
    {
        int n = 0;
        for (const auto& c : p.getModConnections())
            n += c.active();
        return n;
    }
}

TEST_CASE ("Modulation parameters: connections' source and destination are structural, the amount is automatable", "[modulation][state]")
{
    ScratchpadProcessor p;

    for (const auto& ids : params::mod)
    {
        REQUIRE_FALSE (p.apvts.getParameter (ids.source)->isAutomatable());
        REQUIRE_FALSE (p.apvts.getParameter (ids.dest)->isAutomatable());
        REQUIRE (p.apvts.getParameter (ids.amount)->isAutomatable());
    }

    for (const auto& ids : params::lfo)
    {
        REQUIRE_FALSE (p.apvts.getParameter (ids.enabled)->isAutomatable());
        REQUIRE_FALSE (p.apvts.getParameter (ids.shape)->isAutomatable());
        REQUIRE_FALSE (p.apvts.getParameter (ids.retrigger)->isAutomatable());
        REQUIRE (p.apvts.getParameter (ids.rate)->isAutomatable());
        REQUIRE (p.apvts.getParameter (ids.fade)->isAutomatable());
    }
}

TEST_CASE ("The choice lists the parameters offer line up with the engine's enums", "[modulation][state]")
{
    REQUIRE (params::modSourceNames().size() == engine::numModSources);
    REQUIRE (params::modDestNames().size() == engine::numModDests);
    REQUIRE (params::lfoShapeNames().size() == dsp::numLfoShapes);
    REQUIRE (params::modSourceNames()[(int) ModSource::filterEnv] == "Filter Envelope");
    REQUIRE (params::modDestNames()[(int) ModDest::pan] == "Pan");
    REQUIRE (params::modDestNames()[(int) ModDest::lfo2Rate] == "LFO 2 rate");
}

TEST_CASE ("Connecting takes the first free slot at +50% as one named undo step", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);

    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::cutoff) == Result::created);

    const auto c = p.getModConnections();
    REQUIRE (c[0].source == ModSource::lfo1);
    REQUIRE (c[0].dest == ModDest::cutoff);
    REQUIRE (c[0].amount == Catch::Approx (0.5f));
    REQUIRE_FALSE (c[1].active());
    REQUIRE (raw (p, params::mod[0].amount) == 50.0f);

    const auto name = p.undoManager.getUndoDescription();
    REQUIRE (name.startsWith ("Connected L1"));
    REQUIRE (name.contains ("Cutoff"));
    REQUIRE (name.endsWith ("+50%"));

    p.undoManager.undo();
    REQUIRE (activeSlots (p) == 0);

    p.undoManager.redo();
    REQUIRE (p.getModConnections()[0].dest == ModDest::cutoff);

    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::pitchAll) == Result::created);
    REQUIRE (p.getModConnections()[1].dest == ModDest::pitchAll);
}

TEST_CASE ("A source can drive a destination only once, and there are eight slots", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);
    p.addModule (Module::osc3);
    p.addModule (Module::noise);

    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::cutoff) == Result::created);
    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::cutoff) == Result::alreadyConnected);
    REQUIRE (activeSlots (p) == 1);

    const ModDest more[] = { ModDest::resonance, ModDest::pitchAll, ModDest::pitch1, ModDest::pitch2,
                             ModDest::pitch3, ModDest::level1, ModDest::level2 };
    for (const auto d : more)
        REQUIRE (p.connectMod (ModSource::lfo1, d) == Result::created);

    REQUIRE (activeSlots (p) == 8);
    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::noiseLevel) == Result::noFreeSlot);
    REQUIRE (activeSlots (p) == 8);
}

TEST_CASE ("A connection needs both modules in the patch", "[modulation][state]")
{
    ScratchpadProcessor p;

    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::pitchAll) == Result::unavailable); // no LFO yet
    p.addModule (Module::lfo1);
    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::cutoff) == Result::unavailable);   // no filter yet
    REQUIRE (p.connectMod (ModSource::lfo2, ModDest::pitchAll) == Result::unavailable);
    REQUIRE (p.connectMod (ModSource::lfo1, ModDest::pitchAll) == Result::created);

    // Velocity, wheel, aftertouch and key are always there.
    REQUIRE (p.connectMod (ModSource::velocity, ModDest::ampLevel) == Result::created);
    REQUIRE (p.connectMod (ModSource::modWheel, ModDest::pan) == Result::created);
}

TEST_CASE ("Removing a connection frees its slot and is one named, undoable step", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);
    p.connectMod (ModSource::lfo1, ModDest::cutoff);
    p.connectMod (ModSource::lfo1, ModDest::pitchAll);

    p.disconnectMod (0);
    REQUIRE_FALSE (p.getModConnections()[0].active());
    REQUIRE (p.getModConnections()[1].dest == ModDest::pitchAll);
    REQUIRE (p.undoManager.getUndoDescription().startsWith ("Removed L1"));

    p.undoManager.undo();
    REQUIRE (p.getModConnections()[0].dest == ModDest::cutoff);

    // The freed slot is the next one used.
    p.disconnectMod (0);
    REQUIRE (p.connectMod (ModSource::velocity, ModDest::resonance) == Result::created);
    REQUIRE (p.getModConnections()[0].source == ModSource::velocity);
}

TEST_CASE ("Dragging an amount is one undo step named with the old and new amount", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);
    p.connectMod (ModSource::lfo1, ModDest::cutoff);

    p.beginModAmountEdit (0);
    for (const float percent : { 40.0f, 25.0f, 10.0f, -20.0f })
        p.setModAmount (0, percent);
    p.endModAmountEdit (0);

    REQUIRE (p.getModConnections()[0].amount == Catch::Approx (-0.2f));

    (void) p.apvts.copyState(); // the parameters reach the tree, and the undo history, on a timer
    const auto name = p.undoManager.getUndoDescription();
    REQUIRE (name.contains ("+50%"));
    REQUIRE (name.endsWith ("-20%"));

    p.undoManager.undo();
    REQUIRE (p.getModConnections()[0].amount == Catch::Approx (0.5f));
    REQUIRE (p.getModConnections()[0].dest == ModDest::cutoff); // the undo stopped at the drag, not the connection
}

TEST_CASE ("Removing a module removes the connections that touch it, in the same undo step", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);
    p.connectMod (ModSource::lfo1, ModDest::cutoff);
    p.connectMod (ModSource::lfo1, ModDest::pitchAll);
    p.connectMod (ModSource::lfo1, ModDest::pitch2);
    p.connectMod (ModSource::velocity, ModDest::resonance);
    p.connectMod (ModSource::modWheel, ModDest::pan);

    p.removeModule (Module::filter); // takes cutoff and resonance
    REQUIRE (activeSlots (p) == 3);

    p.removeModule (Module::osc2);   // takes osc 2 pitch
    REQUIRE (activeSlots (p) == 2);

    p.removeModule (Module::lfo1);   // takes everything LFO 1 drives
    REQUIRE (activeSlots (p) == 1);
    REQUIRE (p.getModConnections()[4].source == ModSource::modWheel);
    REQUIRE (raw (p, params::lfo[0].rate) == Catch::Approx (params::lfoRateDefaultHz));

    p.undoManager.undo();
    REQUIRE (p.isModulePresent (Module::lfo1));
    REQUIRE (activeSlots (p) == 2);
    REQUIRE (p.getModConnections()[1].dest == ModDest::pitchAll);
}

TEST_CASE ("A new patch clears every connection and LFO", "[modulation][state]")
{
    ScratchpadProcessor p;
    build (p);
    p.connectMod (ModSource::lfo1, ModDest::cutoff);

    p.newPatch();
    REQUIRE (activeSlots (p) == 0);
    REQUIRE_FALSE (p.isModulePresent (Module::lfo1));
}

TEST_CASE ("Connections survive a save and load; a state saved by M2 loads with none", "[modulation][state][migration]")
{
    ScratchpadProcessor a;
    build (a);
    a.connectMod (ModSource::lfo1, ModDest::cutoff);
    a.connectMod (ModSource::modWheel, ModDest::pan);
    a.beginModAmountEdit (1);
    a.setModAmount (1, -35.0f);
    a.endModAmountEdit (1);
    set (a, params::lfo[0].shape, 4.0f);
    set (a, params::lfo[0].rate, 7.5f);
    set (a, params::lfo[0].retrigger, 1.0f);

    juce::MemoryBlock block;
    a.getStateInformation (block);

    ScratchpadProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());

    const auto c = b.getModConnections();
    REQUIRE (c[0].source == ModSource::lfo1);
    REQUIRE (c[0].dest == ModDest::cutoff);
    REQUIRE (c[1].source == ModSource::modWheel);
    REQUIRE (c[1].amount == Catch::Approx (-0.35f));
    REQUIRE (b.isModulePresent (Module::lfo1));
    REQUIRE (raw (b, params::lfo[0].shape) == 4.0f);
    REQUIRE (raw (b, params::lfo[0].rate) == Catch::Approx (7.5f).epsilon (0.001));
    REQUIRE (raw (b, params::lfo[0].retrigger) == 1.0f);

    // M2's state: none of the M3 parameters, schema 2.
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
    {
        const auto id = xml->getChildElement (i)->getStringAttribute ("id");
        if (id.startsWith ("lfo") || id.startsWith ("mod"))
            xml->removeChildElement (xml->getChildElement (i), true);
    }
    xml->setAttribute (patch::schemaVersionAttribute, 2);

    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    ScratchpadProcessor target;
    build (target);
    target.connectMod (ModSource::lfo1, ModDest::pitchAll);
    target.setStateInformation (old.getData(), (int) old.getSize());

    REQUIRE (target.isModulePresent (Module::filter));
    REQUIRE_FALSE (target.isModulePresent (Module::lfo1));
    REQUIRE (activeSlots (target) == 0);
}

TEST_CASE ("Connections reach the engine: velocity on amp level at -100% silences a hard note", "[processor][modulation]")
{
    const auto peakFor = [] (bool connected)
    {
        ScratchpadProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        set (p, params::osc1Wave, 0.0f);
        set (p, params::osc1Level, 0.0f);

        if (connected)
        {
            p.connectMod (ModSource::velocity, ModDest::ampLevel);
            set (p, params::mod[0].amount, -100.0f);
        }

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);

        midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        float peak = 0.0f;

        for (int block = 0; block < 20; ++block)
        {
            p.processBlock (buffer, midi);
            midi.clear();

            if (block >= 10)
                peak = juce::jmax (peak, buffer.getMagnitude (0, 512));
        }

        return peak;
    };

    REQUIRE (peakFor (false) > 0.5f);
    REQUIRE (peakFor (true) < 0.01f);
}

TEST_CASE ("The editor's mod wheel reaches a pan connection through the UI event queue", "[processor][modulation]")
{
    ScratchpadProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    set (p, params::osc1Wave, 0.0f);
    set (p, params::osc1Level, 0.0f);
    p.connectMod (ModSource::modWheel, ModDest::pan);
    set (p, params::mod[0].amount, 100.0f);

    plugin::UiEvent wheel;
    wheel.type = plugin::UiEvent::Type::modWheel;
    wheel.value = 1.0f;
    p.getUiEvents().push (wheel);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);

    for (int block = 0; block < 20; ++block)
    {
        p.processBlock (buffer, midi);
        midi.clear();
    }

    REQUIRE (buffer.getMagnitude (0, 0, 512) < 0.001f);
    REQUIRE (buffer.getMagnitude (1, 0, 512) > 0.5f);
}
