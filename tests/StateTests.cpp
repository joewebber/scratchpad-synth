#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

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

//==============================================================================
// M5: oscillator extras, effects, tempo sync, effect order, schema 5.

namespace
{
    struct ChangeCounter : juce::ChangeListener
    {
        void changeListenerCallback (juce::ChangeBroadcaster*) override { ++count; }
        int count = 0;
    };

    using engine::EffectKind;
    using Order = std::array<EffectKind, engine::numEffects>;
    constexpr Order defaultOrder { EffectKind::distortion, EffectKind::chorus, EffectKind::delay, EffectKind::reverb };

    /** True for every parameter the M5 milestone added. */
    bool isM5Id (const juce::String& id)
    {
        for (const auto* prefix : { "distortion.", "chorus.", "delay.", "reverb.", "ring.", "fm." })
            if (id.startsWith (prefix))
                return true;

        for (const auto* suffix : { ".unison", ".unisonDetune", ".unisonSpread", ".antiAlias", ".sync", ".division" })
            if (id.endsWith (suffix))
                return true;

        return false;
    }

    std::unique_ptr<juce::XmlElement> savedXml (ScratchpadProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    }

    void load (ScratchpadProcessor& p, const juce::XmlElement& xml)
    {
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (xml, block);
        p.setStateInformation (block.getData(), (int) block.getSize());
    }
}

TEST_CASE ("Every M5 parameter exists with its contract default", "[state][m5]")
{
    ScratchpadProcessor p;

    struct Expected { const char* id; float value; };
    std::vector<Expected> expected {
        { params::ringLevel, -60.0f }, { params::fmAmount, 0.0f }, { params::fmTarget, 0.0f },
        { params::distortionEnabled, 0.0f }, { params::distortionDrive, 12.0f }, { params::distortionTone, 50.0f }, { params::distortionMix, 100.0f },
        { params::chorusEnabled, 0.0f }, { params::chorusRate, 0.8f }, { params::chorusDepth, 50.0f }, { params::chorusMix, 50.0f },
        { params::delayEnabled, 0.0f }, { params::delayTime, 350.0f }, { params::delaySync, 0.0f }, { params::delayDivision, 3.0f },
        { params::delayFeedback, 40.0f }, { params::delayPingPong, 0.0f }, { params::delayMix, 35.0f },
        { params::reverbEnabled, 0.0f }, { params::reverbSize, 50.0f }, { params::reverbDecay, 2.0f }, { params::reverbDamping, 50.0f },
        { params::reverbMix, 30.0f },
    };

    for (const auto& o : params::osc)
    {
        expected.push_back ({ o.unison, 1.0f });
        expected.push_back ({ o.unisonDetune, 20.0f });
        expected.push_back ({ o.unisonSpread, 50.0f });
        expected.push_back ({ o.antiAlias, 1.0f });

        if (o.sync != nullptr)
            expected.push_back ({ o.sync, 0.0f });
    }

    for (const auto& l : params::lfo)
    {
        expected.push_back ({ l.sync, 0.0f });
        expected.push_back ({ l.division, 3.0f });
    }

    for (const auto& e : expected)
    {
        INFO (e.id);
        REQUIRE (p.apvts.getParameter (e.id) != nullptr);
        REQUIRE (raw (p, e.id) == Catch::Approx (e.value).epsilon (0.001));
    }

    for (const auto* key : params::effectKeys)
    {
        INFO (key);
        REQUIRE (p.apvts.getParameter (juce::String (key) + ".bypass") != nullptr);
        REQUIRE_FALSE (p.apvts.getParameter (juce::String (key) + ".bypass")->isAutomatable());
        REQUIRE_FALSE (p.apvts.getParameter (juce::String (key) + ".enabled")->isAutomatable());
        REQUIRE_FALSE (p.isBypassed (key));
    }

    // Presence flags are structural; knobs, sync and the oscillator extras are for playing.
    for (const char* id : { params::delaySync, params::delayTime, params::delayPingPong, params::reverbMix, params::ringLevel,
                            params::fmAmount, params::fmTarget, params::osc[1].sync, params::osc[2].sync, params::osc[0].unison,
                            params::osc[0].antiAlias, params::lfo[0].sync, params::lfo[1].division })
    {
        INFO (id);
        REQUIRE (p.apvts.getParameter (id)->isAutomatable());
    }

    REQUIRE (params::fmTargetNames().size() == 3);
    REQUIRE (params::tempoDivisionNames().size() == (int) params::tempoDivisionBeats.size());
    REQUIRE (params::tempoDivisionNames()[params::tempoDivisionDefault] == "1/4");
}

TEST_CASE ("Tempo sync resolves a division to an LFO rate and a delay time", "[state][m5][sync]")
{
    // 1/4 at 120 bpm is one beat: 2 Hz, 500 ms.
    REQUIRE (params::lfoSyncRateHz (120.0, 3) == Catch::Approx (2.0f));
    REQUIRE (params::delaySyncMs (120.0, 3) == Catch::Approx (500.0f));

    // 1/8 is half a beat, one bar is four.
    REQUIRE (params::lfoSyncRateHz (120.0, 2) == Catch::Approx (4.0f));
    REQUIRE (params::delaySyncMs (90.0, 2) == Catch::Approx (333.333f).epsilon (0.001));
    REQUIRE (params::lfoSyncRateHz (120.0, 5) == Catch::Approx (0.5f));
    REQUIRE (params::delaySyncMs (120.0, 5) == Catch::Approx (2000.0f));

    // 1/32 at 120 bpm: 62.5 ms, 16 Hz.
    REQUIRE (params::delaySyncMs (120.0, 0) == Catch::Approx (62.5f));
    REQUIRE (params::lfoSyncRateHz (120.0, 0) == Catch::Approx (16.0f));

    // Out-of-range results are limited: four bars is far longer than the delay's 2 s, and an LFO tops out at 50 Hz.
    REQUIRE (params::delaySyncMs (120.0, 7) == 2000.0f);
    REQUIRE (params::delaySyncMs (400.0, 0) == Catch::Approx (18.75f));
    REQUIRE (params::lfoSyncRateHz (999.0, 0) == params::lfoRateMaxHz);
    REQUIRE (params::lfoSyncRateHz (20.0, 7) == Catch::Approx (20.0f / 60.0f / 16.0f)); // 0.0208 Hz, inside the LFO's range
}

TEST_CASE ("A host with no tempo counts as 120 bpm", "[state][m5][sync]")
{
    REQUIRE (params::delaySyncMs (0.0, 3) == params::delaySyncMs (120.0, 3));
    REQUIRE (params::delaySyncMs (-5.0, 3) == params::delaySyncMs (120.0, 3));
    REQUIRE (params::delaySyncMs (std::nan (""), 3) == params::delaySyncMs (120.0, 3));
    REQUIRE (params::lfoSyncRateHz (0.0, 3) == Catch::Approx (2.0f));

    ScratchpadProcessor p;
    REQUIRE (p.getBpm() == 120.0);
}

TEST_CASE ("Synced delay time reaches the patch, and a bypassed effect is absent", "[state][m5][sync]")
{
    ScratchpadProcessor p;
    p.pickWave (2);
    p.addModule (Module::delay);
    set (p, params::delayTime, 900.0f);
    REQUIRE (p.describePatch().contains ("delay of 900 ms"));

    set (p, params::delaySync, 1.0f);
    set (p, params::delayDivision, 3.0f); // 1/4 at the standalone 120 bpm
    REQUIRE (p.describePatch().contains ("delay of 500 ms"));

    set (p, params::delayDivision, 4.0f); // 1/2
    REQUIRE (p.describePatch().contains ("delay of 1.00 s"));

    p.setBypassed ("delay", true);
    REQUIRE (p.isBypassed ("delay"));
    REQUIRE (p.undoManager.getUndoDescription() == "Bypassed Delay");
    REQUIRE_FALSE (p.describePatch().contains ("delay"));

    p.setBypassed ("delay", false);
    REQUIRE (p.describePatch().contains ("delay of 1.00 s"));
}

TEST_CASE ("Adding and removing an effect is one named undo step and resets its parameters", "[state][m5][modules]")
{
    ScratchpadProcessor p;
    p.pickWave (2);

    const std::array<std::pair<Module, const char*>, 4> effects {{
        { Module::distortion, "Distortion" }, { Module::chorus, "Chorus" }, { Module::delay, "Delay" }, { Module::reverb, "Reverb" }
    }};

    for (const auto& [effect, name] : effects)
    {
        INFO (name);
        REQUIRE (ScratchpadProcessor::moduleName (effect) == name);
        REQUIRE_FALSE (p.isModulePresent (effect));

        p.addModule (effect);
        REQUIRE (p.isModulePresent (effect));
        REQUIRE (p.undoManager.getUndoDescription() == juce::String ("Added ") + name);
    }

    REQUIRE (p.getEffectOrder() == defaultOrder);

    set (p, params::delayTime, 900.0f);
    set (p, params::delayFeedback, 80.0f);
    p.setBypassed ("delay", true);
    p.moveEffect (2, 0);
    p.undoManager.beginNewTransaction();

    p.removeModule (Module::delay);
    REQUIRE_FALSE (p.isModulePresent (Module::delay));
    REQUIRE (p.undoManager.getUndoDescription() == "Removed Delay");
    REQUIRE (raw (p, params::delayTime) == Catch::Approx (params::delayTimeDefaultMs));
    REQUIRE (raw (p, params::delayFeedback) == Catch::Approx (40.0f));
    REQUIRE_FALSE (p.isBypassed ("delay"));

    // Removing keeps the effect's slot in the chain.
    REQUIRE (p.getEffectOrder() == Order { EffectKind::delay, EffectKind::distortion, EffectKind::chorus, EffectKind::reverb });
    REQUIRE (p.isModulePresent (Module::chorus));

    p.undoManager.undo();
    REQUIRE (p.isModulePresent (Module::delay));
    REQUIRE (raw (p, params::delayTime) == Catch::Approx (900.0f).epsilon (0.001));
    REQUIRE (p.isBypassed ("delay"));
}

TEST_CASE ("Adding or removing an oscillator resets its unison and sync", "[state][m5][modules]")
{
    ScratchpadProcessor p;
    p.pickWave (2);
    set (p, params::osc[1].unison, 5.0f);
    set (p, params::osc[1].unisonDetune, 60.0f);
    set (p, params::osc[1].sync, 1.0f);
    set (p, params::osc[1].antiAlias, 0.0f);

    p.addModule (Module::osc2);
    REQUIRE (raw (p, params::osc[1].unison) == 1.0f);
    REQUIRE (raw (p, params::osc[1].unisonDetune) == Catch::Approx (20.0f));
    REQUIRE (raw (p, params::osc[1].sync) == 0.0f);
    REQUIRE (raw (p, params::osc[1].antiAlias) == 1.0f);

    set (p, params::osc[1].unison, 3.0f);
    set (p, params::osc[1].sync, 1.0f);
    p.removeModule (Module::osc2);
    REQUIRE (raw (p, params::osc[1].unison) == 1.0f);
    REQUIRE (raw (p, params::osc[1].sync) == 0.0f);
}

TEST_CASE ("The effect order starts as distortion, chorus, delay, reverb", "[state][m5][order]")
{
    ScratchpadProcessor p;
    REQUIRE (p.getEffectOrder() == defaultOrder);
}

TEST_CASE ("Moving an effect is one named undo step, and undo and redo restore the order", "[state][m5][order]")
{
    ScratchpadProcessor p;

    p.moveEffect (2, 1);
    REQUIRE (p.getEffectOrder() == Order { EffectKind::distortion, EffectKind::delay, EffectKind::chorus, EffectKind::reverb });
    REQUIRE (p.undoManager.getUndoDescription() == "Moved Delay before Chorus");

    p.moveEffect (0, 3);
    REQUIRE (p.getEffectOrder() == Order { EffectKind::delay, EffectKind::chorus, EffectKind::reverb, EffectKind::distortion });
    REQUIRE (p.undoManager.getUndoDescription() == "Moved Distortion after Reverb");

    p.moveEffect (3, 0);
    REQUIRE (p.undoManager.getUndoDescription() == "Moved Distortion before Delay");

    p.undoManager.undo();
    p.undoManager.undo();
    REQUIRE (p.getEffectOrder() == Order { EffectKind::distortion, EffectKind::delay, EffectKind::chorus, EffectKind::reverb });

    p.undoManager.undo();
    REQUIRE (p.getEffectOrder() == defaultOrder);

    p.undoManager.redo();
    REQUIRE (p.getEffectOrder() == Order { EffectKind::distortion, EffectKind::delay, EffectKind::chorus, EffectKind::reverb });

    // Nonsense moves do nothing and add no step.
    const auto before = p.undoManager.getUndoDescription();
    p.moveEffect (1, 1);
    p.moveEffect (-1, 2);
    p.moveEffect (0, 4);
    REQUIRE (p.undoManager.getUndoDescription() == before);
}

TEST_CASE ("The effects broadcaster fires on a move, an undo, a new patch and a loaded state", "[state][m5][order]")
{
    ScratchpadProcessor p;
    ChangeCounter counter;
    p.getEffectsBroadcaster().addChangeListener (&counter);

    const auto fired = [&]
    {
        counter.count = 0;
        p.getEffectsBroadcaster().dispatchPendingMessages();
        return counter.count > 0;
    };

    (void) fired(); // anything queued by construction

    p.moveEffect (3, 0);
    REQUIRE (fired());

    p.undoManager.undo();
    REQUIRE (fired());

    p.undoManager.redo();
    REQUIRE (fired());

    p.newPatch();
    REQUIRE (fired());
    REQUIRE (p.getEffectOrder() == defaultOrder);

    ScratchpadProcessor source;
    source.moveEffect (1, 2);
    auto xml = savedXml (source);
    load (p, *xml);
    REQUIRE (fired());
    REQUIRE (p.getEffectOrder() == source.getEffectOrder());

    p.getEffectsBroadcaster().removeChangeListener (&counter);
}

TEST_CASE ("A new patch resets the effect order, and undo brings it back", "[state][m5][order]")
{
    ScratchpadProcessor p;
    p.moveEffect (0, 2);
    const auto moved = p.getEffectOrder();
    REQUIRE (moved != defaultOrder);
    p.undoManager.beginNewTransaction();

    p.newPatch();
    REQUIRE (p.getEffectOrder() == defaultOrder);

    p.undoManager.undo();
    REQUIRE (p.getEffectOrder() == moved);
}

TEST_CASE ("State with effects, unison, sync, ring and FM survives a save and load", "[state][m5]")
{
    ScratchpadProcessor a;
    a.pickWave (2);
    a.addModule (Module::osc2);
    a.addModule (Module::delay);
    a.addModule (Module::reverb);
    a.moveEffect (3, 0);
    set (a, params::osc[1].unison, 4.0f);
    set (a, params::osc[1].sync, 1.0f);
    set (a, params::osc[0].antiAlias, 0.0f);
    set (a, params::ringLevel, -12.0f);
    set (a, params::fmAmount, 35.0f);
    set (a, params::fmTarget, 2.0f);
    set (a, params::delayTime, 640.0f);
    set (a, params::delayPingPong, 1.0f);
    set (a, params::lfo[1].sync, 1.0f);
    set (a, params::lfo[1].division, 6.0f);
    a.setBypassed ("reverb", true);

    auto xml = savedXml (a);
    REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == 5);
    REQUIRE (xml->getStringAttribute (params::effectOrderProperty) == "3,0,1,2");

    ScratchpadProcessor b;
    load (b, *xml);

    REQUIRE (b.getEffectOrder() == Order { EffectKind::reverb, EffectKind::distortion, EffectKind::chorus, EffectKind::delay });
    REQUIRE (b.isModulePresent (Module::delay));
    REQUIRE (b.isModulePresent (Module::reverb));
    REQUIRE_FALSE (b.isModulePresent (Module::chorus));
    REQUIRE (b.isBypassed ("reverb"));
    REQUIRE (raw (b, params::osc[1].unison) == 4.0f);
    REQUIRE (raw (b, params::osc[1].sync) == 1.0f);
    REQUIRE (raw (b, params::osc[0].antiAlias) == 0.0f);
    REQUIRE (raw (b, params::ringLevel) == Catch::Approx (-12.0f).epsilon (0.001));
    REQUIRE (raw (b, params::fmAmount) == Catch::Approx (35.0f).epsilon (0.001));
    REQUIRE (raw (b, params::fmTarget) == 2.0f);
    REQUIRE (raw (b, params::delayTime) == Catch::Approx (640.0f).epsilon (0.001));
    REQUIRE (raw (b, params::delayPingPong) == 1.0f);
    REQUIRE (raw (b, params::lfo[1].sync) == 1.0f);
    REQUIRE (raw (b, params::lfo[1].division) == 6.0f);
}

TEST_CASE ("A malformed effect order falls back to the default", "[state][m5][order]")
{
    for (const char* bad : { "", "0,1,2", "0,1,2,3,3", "0,0,1,2", "4,1,2,3", "a,b,c,d", "0 1 2 3", "-1,1,2,3", "10,1,2,3" })
    {
        INFO (bad);
        ScratchpadProcessor source;
        source.moveEffect (0, 3);
        auto xml = savedXml (source);
        xml->setAttribute (params::effectOrderProperty, bad);

        ScratchpadProcessor target;
        target.moveEffect (1, 2);
        load (target, *xml);
        REQUIRE (target.getEffectOrder() == defaultOrder);
    }

    // A tree edited in place takes the same road.
    ScratchpadProcessor p;
    p.moveEffect (0, 3);
    p.apvts.state.setProperty (params::effectOrderProperty, "0,0,0,0", nullptr);
    REQUIRE (p.getEffectOrder() == defaultOrder);
}

TEST_CASE ("A state saved by M4 loads with the M5 features at their defaults", "[state][migration][m5]")
{
    ScratchpadProcessor source;
    source.pickWave (3);
    source.addModule (Module::filter);
    set (source, params::osc1Level, -9.0f);
    set (source, params::filterCutoff, 900.0f);
    set (source, params::lfo[0].rate, 6.0f);
    source.moveEffect (0, 3);

    auto xml = savedXml (source);

    // What M4 saved: none of M5's parameters, no effect order, schema 4.
    for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
        if (isM5Id (xml->getChildElement (i)->getStringAttribute ("id")))
            xml->removeChildElement (xml->getChildElement (i), true);

    xml->removeAttribute (params::effectOrderProperty);
    xml->setAttribute (patch::schemaVersionAttribute, 4);

    REQUIRE (patch::migrate (*xml, "ScratchpadSynth") == patch::MigrationResult::migrated);
    REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == patch::currentSchemaVersion);
    xml->setAttribute (patch::schemaVersionAttribute, 4); // let the processor migrate it too

    // The target is mid-patch with M5 features the old state knows nothing about.
    ScratchpadProcessor target;
    target.pickWave (0);
    target.addModule (Module::delay);
    target.addModule (Module::osc2);
    target.moveEffect (3, 0);
    set (target, params::osc[1].unison, 6.0f);
    set (target, params::osc[1].sync, 1.0f);
    set (target, params::fmAmount, 40.0f);
    set (target, params::ringLevel, -10.0f);
    set (target, params::delayTime, 900.0f);
    set (target, params::lfo[0].sync, 1.0f);

    load (target, *xml);

    REQUIRE (raw (target, params::osc1Wave) == 3.0f);
    REQUIRE (raw (target, params::osc1Level) == -9.0f);
    REQUIRE (target.isModulePresent (Module::filter));
    REQUIRE (raw (target, params::filterCutoff) == Catch::Approx (900.0f).epsilon (0.001));
    REQUIRE (raw (target, params::lfo[0].rate) == Catch::Approx (6.0f).epsilon (0.001));

    REQUIRE_FALSE (target.isModulePresent (Module::delay));
    REQUIRE_FALSE (target.isModulePresent (Module::osc2));
    REQUIRE (raw (target, params::osc[1].unison) == 1.0f);
    REQUIRE (raw (target, params::osc[1].sync) == 0.0f);
    REQUIRE (raw (target, params::fmAmount) == 0.0f);
    REQUIRE (raw (target, params::ringLevel) == Catch::Approx (-60.0f));
    REQUIRE (raw (target, params::delayTime) == Catch::Approx (350.0f));
    REQUIRE (raw (target, params::lfo[0].sync) == 0.0f);
    REQUIRE (target.getEffectOrder() == defaultOrder);
}

TEST_CASE ("The processor reports the oscillators' latency to the host", "[processor][m5]")
{
    ScratchpadProcessor p;
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    REQUIRE (p.getLatencySamples() >= dsp::Oscillator::latencySamples);
    REQUIRE (p.getLimiterReductionDb() >= 0.0f);
}
