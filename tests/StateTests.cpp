#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace scratchpad;
using plugin::ScratchpadProcessor;

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
}

TEST_CASE ("A new patch has no oscillator until a wave is picked", "[state]")
{
    ScratchpadProcessor p;
    REQUIRE_FALSE (p.isPatchStarted());
}

TEST_CASE ("Picking a wave adds oscillator 1 as one named undo step", "[state]")
{
    ScratchpadProcessor p;

    p.pickWave (0);
    REQUIRE (p.isPatchStarted());
    REQUIRE (raw (p, params::osc1Wave) == 0.0f);
    REQUIRE (p.undoManager.getUndoDescription() == "Picked sine");

    p.undoManager.undo();
    REQUIRE_FALSE (p.isPatchStarted());

    p.undoManager.redo();
    REQUIRE (p.isPatchStarted());
    REQUIRE (raw (p, params::osc1Wave) == 0.0f);
}

TEST_CASE ("Changing the wave is a named undo step", "[state]")
{
    ScratchpadProcessor p;
    p.pickWave (2);
    p.changeWave (3);

    REQUIRE (raw (p, params::osc1Wave) == 3.0f);
    REQUIRE (p.undoManager.getUndoDescription().contains ("saw"));
    REQUIRE (p.undoManager.getUndoDescription().contains ("pulse"));

    p.undoManager.undo();
    REQUIRE (raw (p, params::osc1Wave) == 2.0f);
    REQUIRE (p.isPatchStarted());
}

TEST_CASE ("New patch restores defaults and can be undone", "[state]")
{
    ScratchpadProcessor p;
    p.pickWave (1);
    set (p, params::osc1Level, -20.0f);
    set (p, params::outVolume, 3.0f);
    p.undoManager.beginNewTransaction();

    p.newPatch();
    REQUIRE_FALSE (p.isPatchStarted());
    REQUIRE (raw (p, params::osc1Level) == params::oscLevelDefaultDb);
    REQUIRE (raw (p, params::outVolume) == params::volumeDefaultDb);

    p.undoManager.undo();
    REQUIRE (p.isPatchStarted());
    REQUIRE (raw (p, params::osc1Wave) == 1.0f);
    REQUIRE (raw (p, params::osc1Level) == -20.0f);
    REQUIRE (raw (p, params::outVolume) == 3.0f);
}

TEST_CASE ("State survives a save and load", "[state]")
{
    ScratchpadProcessor a;
    a.pickWave (3);
    set (a, params::osc1Level, -12.5f);
    set (a, params::outVolume, 2.5f);

    juce::MemoryBlock block;
    a.getStateInformation (block);

    const auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    REQUIRE (xml != nullptr);
    REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == patch::currentSchemaVersion);

    ScratchpadProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());

    REQUIRE (b.isPatchStarted());
    REQUIRE (raw (b, params::osc1Wave) == 3.0f);
    REQUIRE (raw (b, params::osc1Level) == -12.5f);
    REQUIRE (raw (b, params::outVolume) == 2.5f);
}

TEST_CASE ("Saved state from the future or from another product is refused", "[state]")
{
    ScratchpadProcessor source;
    source.pickWave (3);
    juce::MemoryBlock block;
    source.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());

    SECTION ("newer schema")
    {
        xml->setAttribute (patch::schemaVersionAttribute, patch::currentSchemaVersion + 1);
        juce::MemoryBlock future;
        juce::AudioProcessor::copyXmlToBinary (*xml, future);

        ScratchpadProcessor target;
        target.setStateInformation (future.getData(), (int) future.getSize());
        REQUIRE_FALSE (target.isPatchStarted());
    }

    SECTION ("wrong root tag")
    {
        xml->setTagName ("SomethingElse");
        juce::MemoryBlock other;
        juce::AudioProcessor::copyXmlToBinary (*xml, other);

        ScratchpadProcessor target;
        target.setStateInformation (other.getData(), (int) other.getSize());
        REQUIRE_FALSE (target.isPatchStarted());
    }
}

TEST_CASE ("An unversioned tree is migrated to the current schema", "[state]")
{
    juce::XmlElement xml ("ScratchpadSynth");
    REQUIRE (patch::migrate (xml, "ScratchpadSynth") == patch::MigrationResult::migrated);
    REQUIRE (xml.getIntAttribute (patch::schemaVersionAttribute) == patch::currentSchemaVersion);
    REQUIRE (patch::migrate (xml, "ScratchpadSynth") == patch::MigrationResult::upToDate);
}

TEST_CASE ("Module presence is not automatable", "[state]")
{
    ScratchpadProcessor p;
    REQUIRE_FALSE (p.apvts.getParameter (params::osc1Enabled)->isAutomatable());
    REQUIRE_FALSE (p.apvts.getParameter (params::osc1Wave)->isAutomatable());
    REQUIRE (p.apvts.getParameter (params::osc1Level)->isAutomatable());
    REQUIRE (p.apvts.getParameter (params::outVolume)->isAutomatable());
}

TEST_CASE ("The processor renders MIDI and keeps note timing inside the block", "[processor]")
{
    ScratchpadProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.pickWave (0); // sine
    set (p, params::osc1Level, 0.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    // Settle parameter smoothing with a silent block.
    p.processBlock (buffer, midi);

    midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 256);
    buffer.clear();
    p.processBlock (buffer, midi);

    for (int i = 0; i < 256; ++i)
        REQUIRE (buffer.getSample (0, i) == 0.0f);

    REQUIRE (buffer.getMagnitude (0, 256, 256) > 0.5f);
    REQUIRE (buffer.getSample (0, 300) == buffer.getSample (1, 300));
    REQUIRE (p.takeOutputPeak() > 0.5f);
}
