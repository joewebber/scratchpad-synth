#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

// The host calls getStateInformation (and the APVTS timer flushes parameter values into the tree) at any moment.
// Neither may ever add an undo step or clear Redo, whatever the parameter's range.

using namespace scratchpad;
using plugin::ScratchpadProcessor;

namespace
{
    struct Edit
    {
        const char* id;
        float value; // unnormalised
    };

    // Skewed (release, cutoff, LFO rate, delay time), logarithmic-ish, stepped (octave, unison, voices), choice (wave, filter mode, delay division),
    // and plain linear (resonance, mix), so a rounding problem in any kind of range shows.
    const std::vector<Edit>& edits()
    {
        static const std::vector<Edit> list {
            { params::ampEnv.release, 3.75f },
            { params::ampEnv.attack, 0.337f },
            { params::filterCutoff, 1234.5f },
            { params::filterResonance, 71.25f },
            { params::lfo[0].rate, 0.137f },
            { params::delayTime, 641.0f },
            { params::reverbMix, 63.0f },
            { params::osc[0].octave, 2.0f },
            { params::osc[1].unison, 5.0f },
            { params::osc[0].fine, -23.5f },
            { params::osc1Wave, 3.0f },
            { params::filterMode, 2.0f },
            { params::delayDivision, 4.0f },
            { params::outVolume, -4.5f },
            { params::osc1Level, -13.37f },
        };
        return list;
    }

    float raw (ScratchpadProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }

    /** What the Knob and the choice controls do: a gesture through a ParameterAttachment that carries the undo manager. */
    void userEdit (ScratchpadProcessor& p, const Edit& e)
    {
        auto* param = p.apvts.getParameter (e.id);
        juce::ParameterAttachment attachment (*param, [] (float) {}, &p.undoManager);
        p.undoManager.beginNewTransaction (e.id);
        attachment.beginGesture();
        attachment.setValueAsPartOfGesture (e.value); // the attachment takes the real (denormalised) value
        attachment.endGesture();
        (void) p.apvts.copyState(); // the flush the APVTS timer does within 50 ms, so the edit has its own step
    }

    std::vector<float> normalisedValues (ScratchpadProcessor& p)
    {
        std::vector<float> values;

        for (auto* parameter : p.getParameters())
            values.push_back (parameter->getValue());

        return values;
    }

    void requireSameValues (const std::vector<float>& actual, const std::vector<float>& expected)
    {
        REQUIRE (actual.size() == expected.size());

        for (size_t i = 0; i < actual.size(); ++i)
        {
            INFO ("parameter index " << i);
            REQUIRE (actual[i] == expected[i]);
        }
    }
}

TEST_CASE ("Undo, then a state read, then redo restores every value", "[undo][state]")
{
    ScratchpadProcessor p;
    std::vector<std::vector<float>> after; // after[i]: every parameter once step i has been made
    std::vector<const char*> ids;

    for (const auto& e : edits())
    {
        const auto before = normalisedValues (p);
        userEdit (p, e);

        if (normalisedValues (p) != before) // an edit that lands on the current value makes no step
        {
            after.push_back (normalisedValues (p));
            ids.push_back (e.id);
        }
    }

    REQUIRE (after.size() == edits().size());

    const auto last = normalisedValues (p);

    // Back to the start, reading the state at every step as a host does, then forward again.
    for (size_t i = after.size(); i-- > 0;)
    {
        INFO ("undoing " << ids[i]);
        REQUIRE (p.undoManager.canUndo());
        p.undoManager.undo();
        juce::MemoryBlock block;
        p.getStateInformation (block);
        (void) p.apvts.copyState();
        REQUIRE (p.undoManager.canRedo());
    }

    REQUIRE_FALSE (p.undoManager.canUndo());

    for (size_t i = 0; i < after.size(); ++i)
    {
        INFO ("redoing " << ids[i]);
        REQUIRE (p.undoManager.canRedo());
        p.undoManager.redo();
        requireSameValues (normalisedValues (p), after[i]);
        (void) p.apvts.copyState();
        requireSameValues (normalisedValues (p), after[i]);
    }

    REQUIRE_FALSE (p.undoManager.canRedo());
    requireSameValues (normalisedValues (p), last);
}

TEST_CASE ("A state read after any programmatic step and its undo leaves Redo alone", "[undo][state]")
{
    ScratchpadProcessor p;

    for (const auto& e : edits())
    {
        userEdit (p, e);
        (void) p.apvts.copyState();
    }

    const auto edited = normalisedValues (p);

    // New patch, then Open-style module changes: each is one step, each must survive a state read between undo and redo.
    p.newPatch();
    const auto fresh = normalisedValues (p);

    p.undoManager.undo();
    juce::MemoryBlock block;
    p.getStateInformation (block);
    REQUIRE (p.undoManager.canRedo());
    requireSameValues (normalisedValues (p), edited);

    p.undoManager.redo();
    requireSameValues (normalisedValues (p), fresh);

    p.addModule (plugin::Module::filter);
    p.addModule (plugin::Module::lfo1);
    p.addModule (plugin::Module::delay);
    const auto built = normalisedValues (p);

    for (int i = 0; i < 3; ++i)
    {
        p.undoManager.undo();
        (void) p.apvts.copyState();
        REQUIRE (p.undoManager.canRedo());
    }

    for (int i = 0; i < 3; ++i)
        p.undoManager.redo();

    requireSameValues (normalisedValues (p), built);
}

TEST_CASE ("Reading the state adds no undo step and clears nothing", "[undo][state]")
{
    ScratchpadProcessor p;
    userEdit (p, edits()[0]);
    userEdit (p, edits()[2]);
    (void) p.apvts.copyState();

    const auto description = p.undoManager.getUndoDescription();
    const auto values = normalisedValues (p);

    // Undo once so there is something to redo; the reads must keep both stacks as they are.
    p.undoManager.undo();
    const auto redoDescription = p.undoManager.getRedoDescription();
    const auto undoDescription = p.undoManager.getUndoDescription();

    for (int i = 0; i < 5; ++i)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        (void) p.apvts.copyState();
        REQUIRE (p.undoManager.getRedoDescription() == redoDescription);
        REQUIRE (p.undoManager.getUndoDescription() == undoDescription);
    }

    p.undoManager.redo();
    REQUIRE (p.undoManager.getUndoDescription() == description);
    requireSameValues (normalisedValues (p), values);

    // A plain change followed by reads: still exactly one step for it.
    userEdit (p, edits()[4]);
    const auto afterEdit = p.undoManager.getUndoDescription();

    for (int i = 0; i < 3; ++i)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
    }

    REQUIRE (p.undoManager.getUndoDescription() == afterEdit);
    p.undoManager.undo();
    REQUIRE (p.undoManager.getUndoDescription() == description);
}

TEST_CASE ("A value the host sets directly is not an undo step, and reading it back is not one either", "[undo][state]")
{
    ScratchpadProcessor p;
    userEdit (p, edits()[1]);
    const auto description = p.undoManager.getUndoDescription();

    auto* release = p.apvts.getParameter (params::ampEnv.release);
    release->setValueNotifyingHost (release->convertTo0to1 (3.75f));
    (void) p.apvts.copyState();
    juce::MemoryBlock block;
    p.getStateInformation (block);

    REQUIRE (p.undoManager.getUndoDescription() == description);
    REQUIRE (raw (p, params::ampEnv.release) == Catch::Approx (3.75f).epsilon (1.0e-5));
    REQUIRE_FALSE (p.undoManager.canRedo());
}

TEST_CASE ("Recalling a session still clears the undo history", "[undo][state]")
{
    ScratchpadProcessor source;
    userEdit (source, edits()[2]);
    juce::MemoryBlock block;
    source.getStateInformation (block);

    ScratchpadProcessor p;
    userEdit (p, edits()[0]);
    userEdit (p, edits()[1]);
    p.undoManager.undo();
    REQUIRE (p.undoManager.canUndo());
    REQUIRE (p.undoManager.canRedo());

    p.setStateInformation (block.getData(), (int) block.getSize());
    REQUIRE_FALSE (p.undoManager.canUndo());
    REQUIRE_FALSE (p.undoManager.canRedo());
    REQUIRE (raw (p, params::filterCutoff) == Catch::Approx (1234.5f).epsilon (1.0e-4));
}

TEST_CASE ("Every parameter's value survives going into the tree and a flush after an undo to it", "[undo][state]")
{
    // What an undo does to the tree is write an old value straight back. The APVTS then moves the parameter to match
    // and, on its next flush, writes the parameter's own idea of that value if it differs at all. For a skewed or
    // logarithmic range the idea differed by a few ulps (a float pow/log pair does not invert exactly), which became
    // a new undoable action and cleared Redo. So: for every parameter and many values, nothing may be rewritten.
    ScratchpadProcessor p;
    std::uint32_t seed = 12345;
    const auto random01 = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (seed >> 8) / (float) (1u << 24);
    };

    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        auto child = p.apvts.state.getChildWithProperty ("id", ranged->paramID);
        REQUIRE (child.isValid());
        INFO (ranged->paramID);

        for (int i = 0; i < 200; ++i)
        {
            ranged->setValueNotifyingHost (i == 0 ? ranged->getDefaultValue() : random01());
            (void) p.apvts.copyState();
            const auto written = child.getProperty ("value");

            ranged->setValueNotifyingHost (random01());
            (void) p.apvts.copyState();

            child.setProperty ("value", written, nullptr); // what undo does
            (void) p.apvts.copyState();
            REQUIRE ((double) child.getProperty ("value") == (double) written);
        }
    }
}
