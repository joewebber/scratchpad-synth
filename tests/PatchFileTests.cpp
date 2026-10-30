#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginProcessor.h"
#include "ui/TopBar.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

// M6: patch files (`.scratchpad`), the patch name and schema 6.

using namespace scratchpad;
using engine::EffectKind;
using engine::ModDest;
using engine::ModSource;
using plugin::Module;
using plugin::ScratchpadProcessor;

namespace
{
    /** A fresh folder under the system temp directory, removed with the object. Never the real Documents. */
    struct TempFolder
    {
        TempFolder()
            : folder (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("scratchpad-patch-tests", {}, false))
        {
            REQUIRE (folder.createDirectory().wasOk());
        }

        ~TempFolder() { folder.deleteRecursively(); }

        juce::File file (const juce::String& name) const { return folder.getChildFile (name); }

        juce::File folder;
    };

    struct ChangeCounter : juce::ChangeListener
    {
        void changeListenerCallback (juce::ChangeBroadcaster*) override { ++count; }
        int count = 0;
    };

    float raw (ScratchpadProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }

    void set (ScratchpadProcessor& p, const char* id, float unnormalised)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (unnormalised));
    }

    /** Every module, a connection, a moved effect, bypass and solo, and values that are nowhere near a default. */
    void buildRichPatch (ScratchpadProcessor& p)
    {
        for (const auto m : plugin::allModules)
            p.addModule (m);

        set (p, params::osc1Wave, 3.0f);
        set (p, params::osc1Level, -13.37f);
        set (p, params::osc[1].fine, -23.5f);
        set (p, params::osc[1].unison, 5.0f);
        set (p, params::osc[2].semitone, 7.0f);
        set (p, params::osc[2].sync, 1.0f);
        set (p, params::filterCutoff, 1234.5f);
        set (p, params::filterResonance, 71.25f);
        set (p, params::ampEnv.release, 3.75f);
        set (p, params::lfo[1].rate, 0.137f);
        set (p, params::lfo[1].sync, 1.0f);
        set (p, params::lfo[1].division, 5.0f);
        set (p, params::delayTime, 641.0f);
        set (p, params::reverbMix, 63.0f);
        set (p, params::fmAmount, 12.125f);
        set (p, params::outVolume, -4.5f);
        set (p, params::voicingGlide, 0.3f);

        REQUIRE (p.connectMod (ModSource::lfo1, ModDest::cutoff) == ScratchpadProcessor::ConnectResult::created);
        REQUIRE (p.connectMod (ModSource::velocity, ModDest::level2) == ScratchpadProcessor::ConnectResult::created);
        p.setModAmount (1, -37.0f);

        p.moveEffect (3, 0);
        p.moveEffect (2, 1);
        p.setBypassed ("chorus", true);
        p.setBypassed ("filterEnv", true);
        p.setSoloed ("osc2", true);

        // The host timer-flush that normally carries the values into the tree.
        (void) p.apvts.copyState();
    }

    /** Parameters, effect order and name only. Reads nothing that flushes the tree, which an undo/redo test must avoid. */
    void requireSameParameters (ScratchpadProcessor& actual, ScratchpadProcessor& expected)
    {
        const auto& a = actual.getParameters();
        const auto& e = expected.getParameters();
        REQUIRE (a.size() == e.size());

        for (int i = 0; i < a.size(); ++i)
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (a[i]);
            auto* pe = dynamic_cast<juce::RangedAudioParameter*> (e[i]);
            REQUIRE (pa != nullptr);
            INFO (pe->paramID);
            REQUIRE (pa->paramID == pe->paramID);
            // A value goes through text and back, so allow the last bit of a float.
            REQUIRE (pa->getValue() == Catch::Approx (pe->getValue()).margin (1.0e-6));
        }

        REQUIRE (actual.getEffectOrder() == expected.getEffectOrder());
        REQUIRE (actual.getPatchName() == expected.getPatchName());
    }

    /** Everything saved: the parameters, then every tree property and every PARAM child. */
    void requireSamePatch (ScratchpadProcessor& actual, ScratchpadProcessor& expected)
    {
        requireSameParameters (actual, expected);

        // Every tree property (effectOrder, patchName) and every PARAM child.
        const auto ta = actual.apvts.copyState();
        const auto te = expected.apvts.copyState();
        REQUIRE (ta.getNumProperties() == te.getNumProperties());

        for (int i = 0; i < te.getNumProperties(); ++i)
        {
            const auto name = te.getPropertyName (i);
            INFO (name.toString());
            REQUIRE (ta.getProperty (name).toString() == te.getProperty (name).toString());
        }

        REQUIRE (ta.getNumChildren() == te.getNumChildren());

        for (const auto child : te)
        {
            const auto id = child["id"].toString();
            INFO (id);
            const auto match = ta.getChildWithProperty ("id", id);
            REQUIRE (match.isValid());
            REQUIRE ((double) match["value"] == Catch::Approx ((double) child["value"]).epsilon (1.0e-5).margin (1.0e-6));
        }
    }

    std::unique_ptr<juce::XmlElement> savedXml (ScratchpadProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
    }

    void writeXml (const juce::XmlElement& xml, const juce::File& file)
    {
        REQUIRE (xml.writeTo (file));
    }

    /** What a patch looks like from outside: every parameter's normalised value, the order, the name, the undo state. */
    struct Snapshot
    {
        explicit Snapshot (ScratchpadProcessor& p)
            : order (p.getEffectOrder()), name (p.getPatchName()), undo (p.undoManager.getUndoDescription()),
              canRedo (p.undoManager.canRedo())
        {
            for (auto* parameter : p.getParameters())
                values.push_back (parameter->getValue());
        }

        bool operator== (const Snapshot& other) const
        {
            return values == other.values && order == other.order && name == other.name && undo == other.undo
                   && canRedo == other.canRedo;
        }

        std::vector<float> values;
        std::array<EffectKind, engine::numEffects> order;
        juce::String name, undo;
        bool canRedo;
    };
}

TEST_CASE ("A rich patch survives save, load into a fresh processor, parameter for parameter", "[patchfile][state]")
{
    TempFolder temp;
    ScratchpadProcessor a;
    buildRichPatch (a);

    const auto file = temp.file ("Rich lead.scratchpad");
    REQUIRE (a.savePatch (file));
    REQUIRE (file.existsAsFile());

    ScratchpadProcessor b;
    REQUIRE (b.loadPatch (file));

    requireSamePatch (b, a);

    REQUIRE (b.getPatchName() == "Rich lead");
    REQUIRE (b.isBypassed ("chorus"));
    REQUIRE (b.isSoloed ("osc2"));
    REQUIRE (b.getModConnections()[1].source == ModSource::velocity);
    REQUIRE (b.getModConnections()[1].amount == Catch::Approx (-0.37f));
    REQUIRE (b.getEffectOrder() == a.getEffectOrder());
    REQUIRE (b.getEffectOrder() != std::array<EffectKind, 4> { EffectKind::distortion, EffectKind::chorus, EffectKind::delay, EffectKind::reverb });
}

TEST_CASE ("A saved patch file is the host's XML, stamped with schema 6 and carrying the name and effect order", "[patchfile]")
{
    TempFolder temp;
    ScratchpadProcessor p;
    p.addModule (Module::delay);
    p.moveEffect (3, 0);

    const auto file = temp.file ("Pad.scratchpad");
    REQUIRE (p.savePatch (file));

    const auto xml = juce::XmlDocument::parse (file);
    REQUIRE (xml != nullptr);
    REQUIRE (xml->hasTagName ("ScratchpadSynth"));
    REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == 6);
    REQUIRE (patch::currentSchemaVersion == 6);
    REQUIRE (xml->getStringAttribute (patch::nameProperty) == "Pad");
    REQUIRE (xml->getStringAttribute (params::effectOrderProperty) == "3,0,1,2");
    REQUIRE (xml->getChildByAttribute ("id", params::delayEnabled) != nullptr);

    // The same XML the host gets, apart from the name.
    auto session = savedXml (p);
    session->removeAttribute (patch::nameProperty);
    auto onDisk = std::make_unique<juce::XmlElement> (*xml);
    onDisk->removeAttribute (patch::nameProperty);
    REQUIRE (onDisk->isEquivalentTo (session.get(), false));
}

TEST_CASE ("Saving creates the folder, replaces an older file and leaves no temporary file behind", "[patchfile]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("Scratchpad Synth").getChildFile ("Patches").getChildFile ("One.scratchpad");

    ScratchpadProcessor p;
    set (p, params::outVolume, -2.0f);
    REQUIRE (p.savePatch (file));
    REQUIRE (file.existsAsFile());

    set (p, params::outVolume, 1.5f);
    REQUIRE (p.savePatch (file));

    ScratchpadProcessor q;
    REQUIRE (q.loadPatch (file));
    REQUIRE (raw (q, params::outVolume) == Catch::Approx (1.5f));

    REQUIRE (file.getParentDirectory().getNumberOfChildFiles (juce::File::findFiles) == 1);
}

TEST_CASE ("A save that cannot be written reports failure and changes nothing", "[patchfile]")
{
    TempFolder temp;
    ScratchpadProcessor p;
    set (p, params::outVolume, 2.0f);
    REQUIRE (p.savePatch (temp.file ("Named.scratchpad")));
    const Snapshot before (p);

    SECTION ("the folder is really a file")
    {
        const auto blocker = temp.file ("blocker");
        REQUIRE (blocker.replaceWithText ("x"));
        REQUIRE_FALSE (p.savePatch (blocker.getChildFile ("Nope.scratchpad")));
    }

    SECTION ("the target is a folder")
    {
        const auto folderTarget = temp.file ("Folder.scratchpad");
        REQUIRE (folderTarget.createDirectory().wasOk());
        REQUIRE_FALSE (p.savePatch (folderTarget));
        REQUIRE (folderTarget.isDirectory());
    }

    REQUIRE (Snapshot (p) == before);
    REQUIRE (p.getPatchName() == "Named");
}

TEST_CASE ("Opening a patch is one named undo step that undo and redo move across", "[patchfile][undo]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    buildRichPatch (source);
    const auto file = temp.file ("Rich.scratchpad");
    REQUIRE (source.savePatch (file));

    ScratchpadProcessor p;
    set (p, params::osc1Wave, 1.0f);
    set (p, params::filterEnvAmount, 40.0f);
    p.addModule (Module::reverb);
    set (p, params::outVolume, 3.25f);
    p.undoManager.beginNewTransaction();

    REQUIRE (p.loadPatch (file));
    REQUIRE (p.undoManager.getUndoDescription() == "Opened Rich");
    requireSameParameters (p, source);

    p.undoManager.undo();
    p.undoManager.redo();
    p.undoManager.undo();
    REQUIRE (p.undoManager.canRedo());

    // One undo went all the way back: parameters, effect order, name.
    ScratchpadProcessor reference;
    set (reference, params::osc1Wave, 1.0f);
    set (reference, params::filterEnvAmount, 40.0f);
    reference.addModule (Module::reverb);
    set (reference, params::outVolume, 3.25f);
    requireSameParameters (p, reference);
    REQUIRE (p.getPatchName() == juce::String (patch::untitledName));
    REQUIRE (p.isModulePresent (Module::reverb));
    REQUIRE_FALSE (p.isModulePresent (Module::filter));
    REQUIRE (p.undoManager.canRedo());

    p.undoManager.redo();
    requireSamePatch (p, source);
    REQUIRE (p.getPatchName() == "Rich");
    REQUIRE (p.undoManager.getUndoDescription() == "Opened Rich");
}

TEST_CASE ("Opening restores the previous patch's name on undo", "[patchfile][undo]")
{
    TempFolder temp;
    ScratchpadProcessor a;
    REQUIRE (a.savePatch (temp.file ("Alpha.scratchpad")));
    set (a, params::outVolume, -7.0f);
    REQUIRE (a.savePatch (temp.file ("Beta.scratchpad")));

    ScratchpadProcessor p;
    REQUIRE (p.loadPatch (temp.file ("Alpha.scratchpad")));
    REQUIRE (p.getPatchName() == "Alpha");
    REQUIRE (p.loadPatch (temp.file ("Beta.scratchpad")));
    REQUIRE (p.getPatchName() == "Beta");
    REQUIRE (raw (p, params::outVolume) == Catch::Approx (-7.0f));

    p.undoManager.undo();
    REQUIRE (p.getPatchName() == "Alpha");
    REQUIRE (raw (p, params::outVolume) == Catch::Approx (params::volumeDefaultDb));
}

TEST_CASE ("A file that cannot be opened is refused and the current patch is untouched", "[patchfile][errors]")
{
    TempFolder temp;
    ScratchpadProcessor good;
    buildRichPatch (good);
    REQUIRE (good.savePatch (temp.file ("Good.scratchpad")));

    ScratchpadProcessor p;
    set (p, params::filterEnvAmount, 25.0f);
    p.addModule (Module::filter);
    p.moveEffect (0, 2);
    REQUIRE (p.savePatch (temp.file ("Mine.scratchpad")));
    set (p, params::outVolume, 1.0f);
    const Snapshot before (p);

    const auto requireRefused = [&] (const juce::File& f)
    {
        REQUIRE_FALSE (p.loadPatch (f));
        REQUIRE (Snapshot (p) == before);
        REQUIRE (p.getPatchName() == "Mine");
    };

    SECTION ("missing file")
    {
        requireRefused (temp.file ("Nothing here.scratchpad"));
    }

    SECTION ("a folder")
    {
        requireRefused (temp.folder);
    }

    SECTION ("empty file")
    {
        const auto f = temp.file ("Empty.scratchpad");
        REQUIRE (f.replaceWithText (""));
        requireRefused (f);
    }

    SECTION ("not XML")
    {
        const auto f = temp.file ("Text.scratchpad");
        REQUIRE (f.replaceWithText ("this is not a patch"));
        requireRefused (f);
    }

    SECTION ("truncated XML")
    {
        auto text = temp.file ("Good.scratchpad").loadFileAsString();
        const auto f = temp.file ("Cut.scratchpad");
        REQUIRE (f.replaceWithText (text.substring (0, text.length() / 2)));
        requireRefused (f);
    }

    SECTION ("cut off inside the first tag, or never stamped")
    {
        const auto f = temp.file ("Stub.scratchpad");
        REQUIRE (f.replaceWithText ("<ScratchpadSynth"));
        requireRefused (f);

        auto xml = juce::XmlDocument::parse (temp.file ("Good.scratchpad"));
        xml->removeAttribute (patch::schemaVersionAttribute);
        writeXml (*xml, f);
        requireRefused (f);
    }

    SECTION ("XML from something else")
    {
        auto xml = juce::XmlDocument::parse (temp.file ("Good.scratchpad"));
        xml->setTagName ("SomethingElse");
        const auto f = temp.file ("Other.scratchpad");
        writeXml (*xml, f);
        requireRefused (f);
    }

    SECTION ("saved by a newer version")
    {
        auto xml = juce::XmlDocument::parse (temp.file ("Good.scratchpad"));
        xml->setAttribute (patch::schemaVersionAttribute, patch::currentSchemaVersion + 1);
        const auto f = temp.file ("Future.scratchpad");
        writeXml (*xml, f);
        requireRefused (f);

        // The same file at our own version opens, so the refusal was about the version alone.
        xml->setAttribute (patch::schemaVersionAttribute, patch::currentSchemaVersion);
        writeXml (*xml, f);
        REQUIRE (p.loadPatch (f));
    }
}

TEST_CASE ("A refused open adds no undo step", "[patchfile][errors]")
{
    TempFolder temp;
    ScratchpadProcessor p;
    set (p, params::outVolume, 2.0f);
    p.undoManager.beginNewTransaction ("Mine");

    const auto f = temp.file ("Junk.scratchpad");
    REQUIRE (f.replaceWithText ("<ScratchpadSynth"));
    const auto before = p.undoManager.getUndoDescription();
    REQUIRE_FALSE (p.loadPatch (f));
    REQUIRE (p.undoManager.getUndoDescription() == before);
    REQUIRE (raw (p, params::outVolume) == Catch::Approx (2.0f));
}

TEST_CASE ("Opening resets every parameter the file does not mention and clears stale state", "[patchfile][state]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    set (source, params::osc1Wave, 0.0f);
    set (source, params::osc1Level, -9.0f);

    // A file as an older version would have written it: no effects, LFOs, bypass, solo or connections.
    auto xml = savedXml (source);

    for (int i = xml->getNumChildElements() - 1; i >= 0; --i)
    {
        const auto id = xml->getChildElement (i)->getStringAttribute ("id");

        if (id != params::osc1Wave && id != params::osc1Level && id != params::osc1Enabled)
            xml->removeChildElement (xml->getChildElement (i), true);
    }

    xml->removeAttribute (params::effectOrderProperty);
    const auto file = temp.file ("Sparse.scratchpad");
    writeXml (*xml, file);

    ScratchpadProcessor p;
    buildRichPatch (p);
    REQUIRE (p.loadPatch (file));

    ScratchpadProcessor fresh;
    set (fresh, params::osc1Wave, 0.0f);
    set (fresh, params::osc1Level, -9.0f);

    for (auto* parameter : fresh.getParameters())
        REQUIRE (p.getParameters()[parameter->getParameterIndex()]->getValue() == Catch::Approx (parameter->getValue()).margin (1.0e-6));

    for (const auto m : plugin::allModules)
        REQUIRE_FALSE (p.isModulePresent (m));

    for (const auto* key : params::bypassKeys)
    {
        REQUIRE_FALSE (p.isBypassed (key));
        REQUIRE_FALSE (p.isSoloed (key));
    }

    for (const auto& c : p.getModConnections())
        REQUIRE_FALSE (c.active());

    REQUIRE (p.getEffectOrder() == fresh.getEffectOrder());
}

TEST_CASE ("Opening a patch that has no oscillator 1 brings it in", "[patchfile][state]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    set (source, params::osc1Enabled, 0.0f);
    set (source, params::osc1Wave, 1.0f);
    auto xml = savedXml (source);
    REQUIRE (raw (source, params::osc1Enabled) == 0.0f);
    const auto file = temp.file ("NoOsc.scratchpad");
    writeXml (*xml, file);

    ScratchpadProcessor p;
    REQUIRE (p.loadPatch (file));
    REQUIRE (raw (p, params::osc1Enabled) == 1.0f);
    REQUIRE (raw (p, params::osc1Wave) == 1.0f);
}

TEST_CASE ("Values outside a parameter's range and unknown parameters in a file are tolerated", "[patchfile][errors]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    auto xml = savedXml (source);

    for (auto* child : xml->getChildIterator())
    {
        if (child->getStringAttribute ("id") == params::filterCutoff)
            child->setAttribute ("value", 1.0e9);

        if (child->getStringAttribute ("id") == params::outVolume)
            child->setAttribute ("value", "not a number");
    }

    auto* stranger = xml->createNewChildElement ("PARAM");
    stranger->setAttribute ("id", "no.such.parameter");
    stranger->setAttribute ("value", 1.0);
    xml->createNewChildElement ("SomethingElse");

    const auto file = temp.file ("Odd.scratchpad");
    writeXml (*xml, file);

    ScratchpadProcessor p;
    REQUIRE (p.loadPatch (file));

    auto* cutoff = p.apvts.getParameter (params::filterCutoff);
    REQUIRE (cutoff->getValue() <= 1.0f);
    REQUIRE (cutoff->getValue() >= 0.0f);
    REQUIRE (std::isfinite (raw (p, params::outVolume)));
}

TEST_CASE ("A malformed effect order in a file opens as the default order", "[patchfile][order]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    source.moveEffect (0, 3);
    auto xml = savedXml (source);
    xml->setAttribute (params::effectOrderProperty, "0,0,1,2");
    const auto file = temp.file ("BadOrder.scratchpad");
    writeXml (*xml, file);

    ScratchpadProcessor p;
    p.moveEffect (1, 2);
    REQUIRE (p.loadPatch (file));
    REQUIRE (p.getEffectOrder() == std::array<EffectKind, 4> { EffectKind::distortion, EffectKind::chorus, EffectKind::delay, EffectKind::reverb });
}

TEST_CASE ("Opening fires the effects and patch-name broadcasters", "[patchfile]")
{
    TempFolder temp;
    ScratchpadProcessor source;
    source.moveEffect (3, 0);
    REQUIRE (source.savePatch (temp.file ("Moved.scratchpad")));

    ScratchpadProcessor p;
    ChangeCounter effects, names;
    p.getEffectsBroadcaster().addChangeListener (&effects);
    p.getPatchNameBroadcaster().addChangeListener (&names);
    p.getEffectsBroadcaster().dispatchPendingMessages();
    p.getPatchNameBroadcaster().dispatchPendingMessages();
    effects.count = names.count = 0;

    REQUIRE (p.loadPatch (temp.file ("Moved.scratchpad")));
    p.getEffectsBroadcaster().dispatchPendingMessages();
    p.getPatchNameBroadcaster().dispatchPendingMessages();
    REQUIRE (effects.count > 0);
    REQUIRE (names.count > 0);

    p.getEffectsBroadcaster().removeChangeListener (&effects);
    p.getPatchNameBroadcaster().removeChangeListener (&names);
}

//==============================================================================
TEST_CASE ("The patch name starts untitled, follows Save and Open, and New patch resets it", "[patchfile][name]")
{
    TempFolder temp;
    ScratchpadProcessor p;
    REQUIRE (p.getPatchName() == "Untitled patch");

    REQUIRE (p.savePatch (temp.file ("Bell.scratchpad")));
    REQUIRE (p.getPatchName() == "Bell");

    // Saving is not an edit: it adds no undo step.
    REQUIRE_FALSE (p.undoManager.canUndo());

    REQUIRE (p.savePatch (temp.file ("Bell two.scratchpad")));
    REQUIRE (p.getPatchName() == "Bell two");

    ScratchpadProcessor q;
    REQUIRE (q.loadPatch (temp.file ("Bell.scratchpad")));
    REQUIRE (q.getPatchName() == "Bell"); // the file's name, not the name saved inside it
    REQUIRE (q.loadPatch (temp.file ("Bell two.scratchpad")));
    REQUIRE (q.getPatchName() == "Bell two");

    q.newPatch();
    REQUIRE (q.getPatchName() == "Untitled patch");
    REQUIRE (q.undoManager.getUndoDescription() == "New patch");

    q.undoManager.undo();
    REQUIRE (q.getPatchName() == "Bell two");
    q.undoManager.redo();
    REQUIRE (q.getPatchName() == "Untitled patch");
}

TEST_CASE ("A renamed file opens under its new name", "[patchfile][name]")
{
    TempFolder temp;
    ScratchpadProcessor a;
    REQUIRE (a.savePatch (temp.file ("Before.scratchpad")));
    REQUIRE (temp.file ("Before.scratchpad").moveFileTo (temp.file ("After.scratchpad")));

    ScratchpadProcessor b;
    REQUIRE (b.loadPatch (temp.file ("After.scratchpad")));
    REQUIRE (b.getPatchName() == "After");
    REQUIRE (b.undoManager.getUndoDescription() == "Opened After");
}

TEST_CASE ("The patch name is part of the host session and a session without one is untitled", "[patchfile][name]")
{
    TempFolder temp;
    ScratchpadProcessor a;
    REQUIRE (a.savePatch (temp.file ("Session pad.scratchpad")));

    juce::MemoryBlock block;
    a.getStateInformation (block);

    ScratchpadProcessor b;
    b.setStateInformation (block.getData(), (int) block.getSize());
    REQUIRE (b.getPatchName() == "Session pad");

    // A session saved by an earlier version has no name; recalling it must not keep the previous one.
    auto xml = savedXml (a);
    xml->removeAttribute (patch::nameProperty);
    xml->setAttribute (patch::schemaVersionAttribute, 5);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    b.setStateInformation (old.getData(), (int) old.getSize());
    REQUIRE (b.getPatchName() == "Untitled patch");
}

TEST_CASE ("The patch name broadcaster fires for name changes made by save, undo and a recalled session", "[patchfile][name]")
{
    TempFolder temp;
    ScratchpadProcessor p;
    ChangeCounter counter;
    p.getPatchNameBroadcaster().addChangeListener (&counter);

    const auto fired = [&]
    {
        counter.count = 0;
        p.getPatchNameBroadcaster().dispatchPendingMessages();
        return counter.count > 0;
    };

    (void) fired();

    REQUIRE (p.savePatch (temp.file ("A.scratchpad")));
    REQUIRE (fired());

    p.newPatch();
    REQUIRE (fired());

    p.undoManager.undo();
    REQUIRE (fired());
    REQUIRE (p.getPatchName() == "A");

    ScratchpadProcessor other;
    juce::MemoryBlock block;
    other.getStateInformation (block);
    p.setStateInformation (block.getData(), (int) block.getSize());
    REQUIRE (fired());
    REQUIRE (p.getPatchName() == "Untitled patch");

    p.getPatchNameBroadcaster().removeChangeListener (&counter);
}

TEST_CASE ("The patch directory is Documents/Scratchpad Synth/Patches and the extension is added when missing", "[patchfile]")
{
    const auto dir = ScratchpadProcessor::getPatchesDirectory();
    REQUIRE (dir.getFileName() == "Patches");
    REQUIRE (dir.getParentDirectory().getFileName() == "Scratchpad Synth");
    REQUIRE (dir.getParentDirectory().getParentDirectory() == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory);
    REQUIRE (patch::withFileExtension (base.getChildFile ("lead")).getFileName() == "lead.scratchpad");
    REQUIRE (patch::withFileExtension (base.getChildFile ("lead.scratchpad")).getFileName() == "lead.scratchpad");
    REQUIRE (patch::withFileExtension (base.getChildFile ("lead.v2")).getFileName() == "lead.v2.scratchpad");
    REQUIRE (juce::String (patch::fileWildcard) == "*" + juce::String (patch::fileExtension));
}

//==============================================================================
TEST_CASE ("Schema 5 and older trees load, with no patch name, at the current schema", "[patchfile][migration]")
{
    ScratchpadProcessor source;
    buildRichPatch (source);

    for (const int version : { 0, 1, 2, 3, 4, 5 })
    {
        INFO ("schema " << version);
        auto xml = savedXml (source);
        xml->removeAttribute (patch::nameProperty);

        if (version == 0)
            xml->removeAttribute (patch::schemaVersionAttribute);
        else
            xml->setAttribute (patch::schemaVersionAttribute, version);

        REQUIRE (patch::migrate (*xml, "ScratchpadSynth") == patch::MigrationResult::migrated);
        REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == patch::currentSchemaVersion);

        // Session recall: untitled.
        xml->setAttribute (patch::schemaVersionAttribute, version);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (*xml, block);

        ScratchpadProcessor session;
        session.setStateInformation (block.getData(), (int) block.getSize());
        REQUIRE (session.getPatchName() == "Untitled patch");
        REQUIRE (session.getEffectOrder() == source.getEffectOrder());
        REQUIRE (session.isBypassed ("chorus"));
        REQUIRE (raw (session, params::filterCutoff) == Catch::Approx (1234.5f).epsilon (0.001));

        // An old file opened from disk takes its name from the file.
        TempFolder temp;
        const auto file = temp.file ("Old.scratchpad");
        writeXml (*xml, file);

        ScratchpadProcessor opened;
        REQUIRE (opened.loadPatch (file));
        REQUIRE (opened.getPatchName() == "Old");
        REQUIRE (opened.getEffectOrder() == source.getEffectOrder());
        REQUIRE (raw (opened, params::filterCutoff) == Catch::Approx (1234.5f).epsilon (0.001));
        REQUIRE (opened.isSoloed ("osc2"));
    }
}

TEST_CASE ("A tree from a newer schema is refused by migrate and by a recalled session", "[patchfile][migration]")
{
    ScratchpadProcessor source;
    set (source, params::outVolume, 3.0f);
    auto xml = savedXml (source);
    xml->setAttribute (patch::schemaVersionAttribute, patch::currentSchemaVersion + 1);
    REQUIRE (patch::migrate (*xml, "ScratchpadSynth") == patch::MigrationResult::tooNew);
    REQUIRE (xml->getIntAttribute (patch::schemaVersionAttribute) == patch::currentSchemaVersion + 1);

    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (*xml, block);

    ScratchpadProcessor target;
    target.setStateInformation (block.getData(), (int) block.getSize());
    REQUIRE (raw (target, params::outVolume) == Catch::Approx (params::volumeDefaultDb));
}

//==============================================================================
TEST_CASE ("The top bar has Save and Open buttons that explain themselves, and ignores other keys", "[patchfile][ui]")
{
    ScratchpadProcessor p;
    ui::TopBar bar (p);
    bar.setBounds (0, 0, 1476, 52);

    const auto find = [&bar] (const juce::String& title) -> juce::Button*
    {
        for (auto* child : bar.getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child))
                if (button->getTitle() == title)
                    return button;

        return nullptr;
    };

    auto* save = find ("Save patch");
    auto* open = find ("Open patch");
    auto* newPatch = find ("New patch");
    REQUIRE (save != nullptr);
    REQUIRE (open != nullptr);
    REQUIRE (newPatch != nullptr);
    REQUIRE (save->getProperties()["explainId"].toString() == "control.savePatch");
    REQUIRE (open->getProperties()["explainId"].toString() == "control.openPatch");
    REQUIRE (save->isVisible());
    REQUIRE (open->isVisible());

    // Placed after New patch, in order, without overlapping.
    REQUIRE (newPatch->getRight() <= save->getX());
    REQUIRE (save->getRight() <= open->getX());
    REQUIRE (find ("Undo")->getX() >= open->getRight());

    // Only Ctrl/Cmd+S and Ctrl/Cmd+O are shortcuts: a plain key plays a note, other chords are not ours.
    REQUIRE_FALSE (bar.handleKeyPressed (juce::KeyPress ('s', juce::ModifierKeys::noModifiers, 's')));
    REQUIRE_FALSE (bar.handleKeyPressed (juce::KeyPress ('o', juce::ModifierKeys::noModifiers, 'o')));
    REQUIRE_FALSE (bar.handleKeyPressed (juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0)));
    REQUIRE_FALSE (bar.handleKeyPressed (juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    REQUIRE_FALSE (bar.handleKeyPressed (juce::KeyPress ('s', juce::ModifierKeys::altModifier, 0)));
}
