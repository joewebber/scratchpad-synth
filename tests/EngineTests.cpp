#include "TestUtils.h"
#include "engine/SynthEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace scratchpad;
using engine::EngineParams;
using engine::SynthEngine;

namespace
{
    constexpr double sampleRate = 48000.0;

    EngineParams sineParams()
    {
        EngineParams p;
        p.oscEnabled = true;
        p.wave = dsp::Wave::sine;
        p.oscLevelDb = 0.0f;
        p.outputVolumeDb = 0.0f;
        return p;
    }

    struct Rig
    {
        explicit Rig (EngineParams p = sineParams())
        {
            engine.setParams (p);
            engine.prepare (sampleRate);
            engine.setParams (p);
        }

        /** Renders in awkward block sizes, as hosts do. Returns the left channel. */
        std::vector<float> run (int numSamples)
        {
            std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);
            int done = 0;
            int block = 37;
            while (done < numSamples)
            {
                const int n = std::min (block, numSamples - done);
                engine.render (left.data() + done, right.data() + done, n);
                done += n;
                block = block == 37 ? 101 : 37;
            }
            REQUIRE (left == right);
            return left;
        }

        void midi (const juce::MidiMessage& m) { engine.handleMidi (m); }

        SynthEngine engine;
    };

    float peak (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (float s : x)
            p = std::max (p, std::abs (s));
        return p;
    }

    float maxStep (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (size_t i = 1; i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    double measureHz (const std::vector<float>& x)
    {
        std::vector<double> crossings;
        for (size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back ((double) (i - 1) + (double) -x[i - 1] / (double) (x[i] - x[i - 1]));
        REQUIRE (crossings.size() > 10);
        return (double) (crossings.size() - 1) * sampleRate / (crossings.back() - crossings.front());
    }
}

TEST_CASE ("Notes are ignored until the patch has an oscillator", "[engine]")
{
    EngineParams p = sineParams();
    p.oscEnabled = false;
    Rig rig (p);

    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    REQUIRE (peak (rig.run (4800)) == 0.0f);
}

TEST_CASE ("Wave-picker previews sound even before the patch has an oscillator", "[engine]")
{
    EngineParams p = sineParams();
    p.oscEnabled = false;
    Rig rig (p);

    rig.engine.previewNoteOn (57, dsp::Wave::sine);
    const auto out = rig.run (9600);
    REQUIRE (peak (out) > 0.9f);
    REQUIRE (std::abs (testutil::centsBetween (measureHz (out), 220.0)) < 0.5);

    rig.engine.previewNoteOff (57);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("A held note plays at pitch and falls silent after release", "[engine]")
{
    Rig rig;

    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    const auto held = rig.run (24000);
    REQUIRE (std::abs (testutil::centsBetween (measureHz (held), 440.0)) < 0.5);
    REQUIRE (peak (held) > 0.95f);

    rig.midi (juce::MidiMessage::noteOff (1, 69));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
    REQUIRE (rig.engine.getAllocator().countActive() == 0);
}

TEST_CASE ("Note on, note off and voice steal do not click", "[engine][clicks]")
{
    // A 1 kHz sine at full level moves at most 0.131 per sample on its own;
    // the gate ramp adds up to 0.011 more. Anything bigger is a click.
    constexpr float limit = 0.16f;

    EngineParams p = sineParams();
    Rig rig (p);
    rig.engine.getAllocator().setActiveVoices (1);

    std::vector<float> all;
    auto append = [&all] (const std::vector<float>& v) { all.insert (all.end(), v.begin(), v.end()); };

    append (rig.run (333));
    rig.midi (juce::MidiMessage::noteOn (1, 83, 1.0f)); // ~987 Hz, mid-sample start
    append (rig.run (1777));
    rig.midi (juce::MidiMessage::noteOn (1, 84, 1.0f)); // steals the only voice
    append (rig.run (1999));
    rig.midi (juce::MidiMessage::noteOff (1, 84));
    append (rig.run (999));

    REQUIRE (peak (all) > 0.9f);
    REQUIRE (maxStep (all) < limit);
}

TEST_CASE ("Switching the wave while a note is held crossfades over 10 ms", "[engine][clicks]")
{
    // Three rigs play the same note from the same moment: one keeps the sine,
    // one has the saw from the start, one switches from sine to saw mid-note.
    // The switched output must glide from the first to the second.
    constexpr int fadeSamples = 480;

    auto sawParams = sineParams();
    sawParams.wave = dsp::Wave::saw;

    Rig sine (sineParams()), saw (sawParams), switched (sineParams());
    for (auto* r : { &sine, &saw, &switched })
    {
        r->midi (juce::MidiMessage::noteOn (1, 45, 1.0f));
        r->run (4800);
    }

    switched.engine.setParams (sawParams);

    const auto a = sine.run (1200);
    const auto b = saw.run (1200);
    const auto c = switched.run (1200);

    for (int i = 0; i < 1200; ++i)
    {
        const float x = std::min (1.0f, (float) i / (float) fadeSamples);
        const float expected = a[(size_t) i] + (b[(size_t) i] - a[(size_t) i]) * x;
        INFO ("sample " << i);
        REQUIRE (std::abs (c[(size_t) i] - expected) < 0.01f);
    }
}

TEST_CASE ("Pitch bend moves the pitch by the bend range", "[engine]")
{
    Rig rig;
    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    rig.run (2400);

    rig.midi (juce::MidiMessage::pitchWheel (1, 16383)); // fully up, default range +2 semitones
    rig.run (2400);
    const double up = measureHz (rig.run (24000));
    // 16383 is 8191/8192 of full scale, so just under 200 cents.
    REQUIRE (std::abs (testutil::centsBetween (up, 440.0) - 200.0 * 8191.0 / 8192.0) < 0.5);

    rig.midi (juce::MidiMessage::pitchWheel (1, 8192));
    rig.run (2400);
    REQUIRE (std::abs (testutil::centsBetween (measureHz (rig.run (24000)), 440.0)) < 0.5);
}

TEST_CASE ("CC123 releases every held note", "[engine]")
{
    Rig rig;
    for (int n : { 60, 64, 67 })
        rig.midi (juce::MidiMessage::noteOn (1, n, 1.0f));
    rig.run (2000);

    rig.midi (juce::MidiMessage::allNotesOff (1));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("The sustain pedal (CC64) holds notes after key-up", "[engine]")
{
    Rig rig;
    rig.midi (juce::MidiMessage::controllerEvent (1, 64, 127));
    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    rig.run (2000);
    rig.midi (juce::MidiMessage::noteOff (1, 69));
    REQUIRE (peak (rig.run (2000)) > 0.9f);

    rig.midi (juce::MidiMessage::controllerEvent (1, 64, 0));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("Level and output volume are in dB, with the bottom of the range silent", "[engine]")
{
    EngineParams p = sineParams();
    p.oscLevelDb = -6.0f;
    p.outputVolumeDb = 6.0f;
    Rig rig (p);
    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    rig.run (2400);
    REQUIRE (std::abs (peak (rig.run (4800)) - 1.0f) < 0.01f); // -6 dB + 6 dB

    p.oscLevelDb = engine::minLevelDb;
    rig.engine.setParams (p);
    rig.run (4800); // let the 20 ms smoothing finish
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("Losing the oscillator lets held notes fade out", "[engine]")
{
    Rig rig;
    rig.midi (juce::MidiMessage::noteOn (1, 69, 1.0f));
    rig.run (2000);

    auto p = sineParams();
    p.oscEnabled = false;
    rig.engine.setParams (p);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}
