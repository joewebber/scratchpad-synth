#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace scratchpad;
using plugin::Module;
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

TEST_CASE ("Adding a module is one named undo step and removing it restores the defaults", "[state][modules]")
{
    ScratchpadProcessor p;
    p.pickWave (2);
    REQUIRE_FALSE (p.isModulePresent (Module::filter));

    p.addModule (Module::filter);
    REQUIRE (p.isModulePresent (Module::filter));
    REQUIRE (p.undoManager.getUndoDescription() == "Added Filter");

    set (p, params::filterCutoff, 500.0f);
    p.undoManager.beginNewTransaction();

    p.removeModule (Module::filter);
    REQUIRE_FALSE (p.isModulePresent (Module::filter));
    REQUIRE (p.undoManager.getUndoDescription() == "Removed Filter");
    REQUIRE (raw (p, params::filterCutoff) == Catch::Approx (params::cutoffDefaultHz));

    p.undoManager.undo();
    REQUIRE (p.isModulePresent (Module::filter));
    REQUIRE (raw (p, params::filterCutoff) == Catch::Approx (500.0f));

    p.undoManager.undo(); // the cutoff edit
    p.undoManager.undo(); // "Added Filter"
    REQUIRE_FALSE (p.isModulePresent (Module::filter));
}

TEST_CASE ("A new oscillator takes oscillator 1's wave and starts 7 cents sharp", "[state][modules]")
{
    ScratchpadProcessor p;
    p.pickWave (3);
    set (p, params::osc[1].octave, 2.0f); // stale value from an earlier use of the slot

    p.addModule (Module::osc2);
    REQUIRE (raw (p, params::osc[1].wave) == 3.0f);
    REQUIRE (raw (p, params::osc[1].fine) == params::newOscillatorFineCents);
    REQUIRE (raw (p, params::osc[1].octave) == 0.0f);
    REQUIRE (raw (p, params::osc[1].level) == params::oscLevelDefaultDb);
    REQUIRE (p.isModulePresent (Module::osc2));
    REQUIRE_FALSE (p.isModulePresent (Module::osc3));

    // Adding a module that is already there changes nothing and adds no undo step.
    const auto before = p.undoManager.getUndoDescription();
    p.addModule (Module::osc2);
    REQUIRE (p.undoManager.getUndoDescription() == before);
}

TEST_CASE ("New patch removes every module and resets every parameter", "[state][modules]")
{
    ScratchpadProcessor p;
    p.pickWave (1);

    for (const auto m : plugin::allModules)
        p.addModule (m);

    set (p, params::filterResonance, 80.0f);
    set (p, params::ampEnv.release, 5.0f);
    set (p, params::voicingMode, 2.0f);
    set (p, params::voicingGlide, 0.5f);
    set (p, params::osc[2].semitone, 7.0f);

    p.newPatch();

    for (const auto m : plugin::allModules)
        REQUIRE_FALSE (p.isModulePresent (m));

    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        INFO (ranged->paramID.toStdString());
        REQUIRE (parameter->getValue() == Catch::Approx (parameter->getDefaultValue()).margin (1.0e-6));
    }
}

TEST_CASE ("M2 module presence and choices are not automatable; the knobs are", "[state][modules]")
{
    ScratchpadProcessor p;

    for (const char* id : { params::osc[1].enabled, params::osc[2].enabled, params::noiseEnabled, params::filterEnabled,
                            params::ampEnv.enabled, params::filterEnv.enabled, params::filterMode, params::noiseColour,
                            params::voicingMode })
        REQUIRE_FALSE (p.apvts.getParameter (id)->isAutomatable());

    for (const char* id : { params::filterCutoff, params::filterResonance, params::ampEnv.attack, params::filterEnvAmount,
                            params::voicingGlide, params::osc[1].fine, params::noiseLevel })
        REQUIRE (p.apvts.getParameter (id)->isAutomatable());
}

TEST_CASE ("A state saved by M1 loads with the M2 modules absent and at their defaults", "[state][migration]")
{
    ScratchpadProcessor source;
    source.pickWave (3);
    set (source, params::osc1Level, -9.0f);
    set (source, params::outVolume, 1.5f);

    juce::MemoryBlock block;
    source.getStateInformation (block);
    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());

    // What M1 saved: only its three parameters and the output volume, schema 1.
    for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
    {
        const auto id = xml->getChildElement (i)->getStringAttribute ("id");
        if (id != params::osc1Enabled && id != params::osc1Wave && id != params::osc1Level && id != params::outVolume)
            xml->removeChildElement (xml->getChildElement (i), true);
    }
    xml->setAttribute (patch::schemaVersionAttribute, 1);

    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    // The target is mid-patch, with M2 modules the old state knows nothing about.
    ScratchpadProcessor target;
    target.pickWave (0);
    target.addModule (Module::filter);
    target.addModule (Module::osc3);
    set (target, params::filterCutoff, 300.0f);
    set (target, params::voicingVoices, 3.0f);

    target.setStateInformation (old.getData(), (int) old.getSize());

    REQUIRE (target.isPatchStarted());
    REQUIRE (raw (target, params::osc1Wave) == 3.0f);
    REQUIRE (raw (target, params::osc1Level) == -9.0f);
    REQUIRE (raw (target, params::outVolume) == 1.5f);

    for (const auto m : plugin::allModules)
        REQUIRE_FALSE (target.isModulePresent (m));

    REQUIRE (raw (target, params::filterCutoff) == Catch::Approx (params::cutoffDefaultHz));
    REQUIRE (raw (target, params::voicingVoices) == (float) params::defaultVoices);
}

TEST_CASE ("State with modules survives a save and load", "[state][modules]")
{
    ScratchpadProcessor a;
    a.pickWave (0);
    a.addModule (Module::osc2);
    a.addModule (Module::filter);
    a.addModule (Module::ampEnv);
    set (a, params::filterCutoff, 740.0f);
    set (a, params::filterMode, 2.0f);
    set (a, params::ampEnv.attack, 0.25f);
    set (a, params::voicingMode, 1.0f);

    juce::MemoryBlock block;
    a.getStateInformation (block);

    ScratchpadProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());

    REQUIRE (b.isModulePresent (Module::osc2));
    REQUIRE (b.isModulePresent (Module::filter));
    REQUIRE (b.isModulePresent (Module::ampEnv));
    REQUIRE_FALSE (b.isModulePresent (Module::noise));
    REQUIRE (raw (b, params::filterCutoff) == Catch::Approx (740.0f).epsilon (0.001));
    REQUIRE (raw (b, params::filterMode) == 2.0f);
    REQUIRE (raw (b, params::ampEnv.attack) == Catch::Approx (0.25f).epsilon (0.001));
    REQUIRE (raw (b, params::voicingMode) == 1.0f);
}

TEST_CASE ("Module parameters reach the engine: a low cutoff takes the note away", "[processor][modules]")
{
    const auto render = [] (bool withFilter)
    {
        ScratchpadProcessor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.pickWave (0); // sine
        set (p, params::osc1Level, 0.0f);

        if (withFilter)
        {
            p.addModule (Module::filter);
            set (p, params::filterCutoff, 100.0f); // a sine at 440 Hz is two octaves above this
            set (p, params::filterResonance, 0.0f);
        }

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi); // let the smoothers settle

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

    const float open = render (false);
    const float filtered = render (true);

    REQUIRE (open > 0.5f);
    REQUIRE (filtered < open * 0.05f); // 24 dB/oct, two octaves above cutoff: far more than 26 dB down
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
