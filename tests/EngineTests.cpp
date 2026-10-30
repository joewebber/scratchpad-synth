#include "EngineRig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace enginetest;

TEST_CASE ("Notes are ignored until the patch has an oscillator", "[engine]")
{
    EngineParams p = sineParams();
    p.osc[0].enabled = false;
    Rig rig (p);

    rig.noteOn (69);
    REQUIRE (peak (rig.run (4800)) == 0.0f);
}

TEST_CASE ("Notes are ignored while only a later oscillator is on", "[engine]")
{
    EngineParams p = sineParams();
    p.osc[0].enabled = false;
    p.osc[1].enabled = true;
    p.osc[1].levelDb = 0.0f;
    Rig rig (p);

    rig.noteOn (69);
    REQUIRE (peak (rig.run (4800)) == 0.0f);
}

TEST_CASE ("A held note plays at pitch and falls silent after release", "[engine]")
{
    Rig rig;

    rig.noteOn (69);
    const auto held = rig.run (24000);
    REQUIRE (std::abs (testutil::centsBetween (measureHz (held), 440.0)) < 0.5);
    REQUIRE (peak (held) > 0.95f);

    rig.noteOff (69);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
    REQUIRE (rig.activeVoices() == 0);
}

TEST_CASE ("Note on, note off and voice steal do not click", "[engine][clicks]")
{
    // A 1 kHz sine at full level moves at most 0.131 per sample on its own;
    // the gate ramp adds up to 0.011 more. Anything bigger is a click.
    constexpr float limit = 0.16f;

    EngineParams p = sineParams();
    p.voicing.voices = 1;
    Rig rig (p);

    std::vector<float> all;
    auto append = [&all] (const std::vector<float>& v) { all.insert (all.end(), v.begin(), v.end()); };

    append (rig.run (333));
    rig.noteOn (83); // ~987 Hz, mid-sample start
    append (rig.run (1777));
    rig.noteOn (84); // steals the only voice
    append (rig.run (1999));
    rig.noteOff (84);
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

    // Played at -12 dB: the saw's band-limited overshoot would otherwise reach the safety limiter, whose
    // gain differs between the three rigs and makes the comparison non-linear. The tolerance scales with it.
    constexpr float levelDb = -12.0f;
    const float level = std::pow (10.0f, levelDb / 20.0f);

    auto sineQuiet = sineParams();
    sineQuiet.osc[0].levelDb = levelDb;
    auto sawParams = sineQuiet;
    sawParams.osc[0].wave = dsp::Wave::saw;

    Rig sine (sineQuiet), saw (sawParams), switched (sineQuiet);
    for (auto* r : { &sine, &saw, &switched })
    {
        r->noteOn (45);
        r->run (4800);
    }

    switched.set (sawParams);

    const auto a = sine.run (1200);
    const auto b = saw.run (1200);
    const auto c = switched.run (1200);

    for (int i = 0; i < 1200; ++i)
    {
        const float x = std::min (1.0f, (float) i / (float) fadeSamples);
        const float expected = a[(size_t) i] + (b[(size_t) i] - a[(size_t) i]) * x;
        INFO ("sample " << i);
        REQUIRE (std::abs (c[(size_t) i] - expected) < 0.01f * level);
    }
}

TEST_CASE ("Pitch bend moves the pitch by the bend range", "[engine]")
{
    Rig rig;
    rig.noteOn (69);
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

TEST_CASE ("Pitch bend range comes from the voicing parameters", "[engine]")
{
    auto p = sineParams();
    p.voicing.bendRange = 12.0f;
    Rig rig (p);

    rig.noteOn (60);
    rig.midi (juce::MidiMessage::pitchWheel (1, 0)); // fully down
    rig.run (4800);
    REQUIRE (std::abs (testutil::centsBetween (measureHz (rig.run (24000)), testutil::midiToHz (48.0))) < 0.5);
}

TEST_CASE ("Pitch bend applies to every oscillator", "[engine]")
{
    // Osc 2 alone, an octave up, bent a whole range up: 69 + 12 + 2 semitones.
    auto p = silentParams();
    p.osc[1].enabled = true;
    p.osc[1].wave = dsp::Wave::sine;
    p.osc[1].levelDb = 0.0f;
    p.osc[1].octave = 1;
    Rig rig (p);

    rig.noteOn (69);
    rig.midi (juce::MidiMessage::pitchWheel (1, 16383));
    rig.run (4800);
    const double hz = measureHz (rig.run (24000));
    REQUIRE (std::abs (testutil::centsBetween (hz, testutil::midiToHz (81.0 + 2.0 * 8191.0 / 8192.0))) < 0.5);
}

TEST_CASE ("CC123 releases every held note", "[engine]")
{
    Rig rig;
    for (int n : { 60, 64, 67 })
        rig.noteOn (n);
    rig.run (2000);

    rig.midi (juce::MidiMessage::allNotesOff (1));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("The sustain pedal (CC64) holds notes after key-up", "[engine]")
{
    Rig rig;
    rig.pedal (true);
    rig.noteOn (69);
    rig.run (2000);
    rig.noteOff (69);
    REQUIRE (peak (rig.run (2000)) > 0.9f);

    rig.pedal (false);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("Level and output volume are in dB, with the bottom of the range silent", "[engine]")
{
    EngineParams p = sineParams();
    p.osc[0].levelDb = -12.0f;
    p.outputVolumeDb = 6.0f;
    Rig rig (p);
    rig.noteOn (69);
    rig.run (2400);
    REQUIRE (std::abs (peak (rig.run (4800)) - 0.5011872f) < 0.005f); // -12 dB + 6 dB = -6 dB, clear of the limiter's knee

    p.osc[0].levelDb = engine::minLevelDb;
    rig.set (p);
    rig.run (4800); // let the 20 ms smoothing finish
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("Level changes ramp over 20 ms instead of stepping", "[engine][clicks]")
{
    EngineParams p = sineParams();
    p.osc[0].levelDb = -20.0f;
    Rig rig (p);
    rig.noteOn (36); // 65 Hz, so the sine itself moves slowly
    rig.run (4800);

    p.osc[0].levelDb = 0.0f;
    rig.set (p);
    const auto out = rig.run (4800);

    // A 65 Hz sine at full level moves at most 0.0085 per sample; a step from
    // -20 dB to 0 dB would show up as a jump of up to 0.9.
    REQUIRE (maxStep (out) < 0.02f);
    REQUIRE (peakOf (out, 3000, 1800) > 0.95f);
}

TEST_CASE ("Losing the oscillator lets held notes fade out", "[engine]")
{
    Rig rig;
    rig.noteOn (69);
    rig.run (2000);

    auto p = sineParams();
    p.osc[0].enabled = false;
    rig.set (p);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}

TEST_CASE ("The output is the same in both channels, whatever the patch", "[engine]")
{
    // Rig::run requires left == right on every block; this exercises a busy patch.
    auto p = sineParams();
    p.osc[1].enabled = true;
    p.osc[2].enabled = true;
    p.noise.enabled = true;
    p.filter.enabled = true;
    p.ampEnv.enabled = true;
    Rig rig (p);

    rig.noteOn (48);
    rig.noteOn (55);
    REQUIRE (peak (rig.run (9600)) > 0.1f);
}
