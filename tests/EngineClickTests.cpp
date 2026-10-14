#include "EngineRig.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace enginetest;
using engine::VoiceMode;

/** The "no audible clicks" bar for the subtractive voice. Every scenario plays
    low sines (so the waveform itself moves very little from one sample to the
    next) through the filter, and asserts that nothing in the output steps by
    more than a sine of that size plus the declick ramps can account for.
    A hard edge anywhere would show up as a jump of a sizeable fraction of the
    signal. */
namespace
{
    // A 196 Hz sine at 0.5 moves at most 0.013 per sample; the 2 ms gate adds 0.005.
    constexpr float limit = 0.03f;

    EngineParams clickPatch (bool envelope)
    {
        auto p = sineParams();
        p.osc[0].levelDb = -6.0f;
        p.filter.enabled = true;
        p.filter.mode = dsp::FilterMode::lp24;
        p.filter.cutoffHz = 4000.0f;
        p.filter.resonance = 0.0f;
        p.ampEnv.enabled = envelope;
        p.ampEnv.attack = 0.005f;
        p.ampEnv.decay = 0.1f;
        p.ampEnv.sustain = 0.7f;
        p.ampEnv.release = 0.05f;
        return p;
    }

    struct Capture
    {
        void add (const std::vector<float>& v) { all.insert (all.end(), v.begin(), v.end()); }

        std::vector<float> all;
    };
}

TEST_CASE ("Note on, note off and voice steal through the filter do not click", "[clicks][filter]")
{
    for (bool envelope : { false, true })
    {
        auto p = clickPatch (envelope);
        p.voicing.voices = 1;
        Rig rig (p);
        Capture out;

        out.add (rig.run (333));
        rig.noteOn (43);
        out.add (rig.run (1777));
        rig.noteOn (55); // steals the only voice
        out.add (rig.run (1999));
        rig.noteOff (55);
        out.add (rig.run (1999));

        INFO ("amp envelope " << envelope);
        REQUIRE (peak (out.all) > 0.15f);
        REQUIRE (maxStep (out.all) < limit);
    }
}

TEST_CASE ("Poly voices stealing each other in a busy chord do not click", "[clicks][poly]")
{
    for (bool envelope : { false, true })
    {
        auto p = clickPatch (envelope);
        p.voicing.voices = 3;
        Rig rig (p);
        Capture out;

        for (int n : { 31, 33, 36, 38, 41, 43, 45 }) // seven notes into three voices
        {
            rig.noteOn (n);
            out.add (rig.run (777));
        }
        for (int n : { 31, 33, 36, 38, 41, 43, 45 })
        {
            rig.noteOff (n);
            out.add (rig.run (555));
        }
        out.add (rig.run (4800));

        INFO ("amp envelope " << envelope);
        REQUIRE (peak (out.all) > 0.15f);
        REQUIRE (maxStep (out.all) < 0.04f); // up to three voices at once, plus their ramps
    }
}

TEST_CASE ("Switching between poly, mono and legato mid-note does not click", "[clicks][voicing]")
{
    for (bool envelope : { false, true })
    {
        auto p = clickPatch (envelope);
        Rig rig (p);
        Capture out;

        out.add (rig.run (333));
        rig.noteOn (36);
        rig.noteOn (43);
        out.add (rig.run (2000));

        p.voicing.mode = VoiceMode::mono; // releases both held notes
        rig.set (p);
        out.add (rig.run (1500));

        rig.noteOn (48);
        out.add (rig.run (1500));
        rig.noteOn (52); // a retrigger on a sounding voice, with a pitch jump
        out.add (rig.run (1500));
        rig.noteOff (52); // back to 48
        out.add (rig.run (1500));

        p.voicing.mode = VoiceMode::legato;
        rig.set (p);
        out.add (rig.run (1500));

        rig.noteOn (45);
        rig.noteOn (55); // slides without retriggering
        out.add (rig.run (2000));

        p.voicing.mode = VoiceMode::poly;
        rig.set (p);
        out.add (rig.run (2000));

        INFO ("amp envelope " << envelope);
        REQUIRE (peak (out.all) > 0.15f);
        REQUIRE (maxStep (out.all) < limit);
    }
}

TEST_CASE ("Glide does not click", "[clicks][glide]")
{
    for (auto mode : { VoiceMode::mono, VoiceMode::legato })
    {
        auto p = clickPatch (true);
        p.voicing.mode = mode;
        p.voicing.glideSeconds = 0.2f;
        Rig rig (p);
        Capture out;

        rig.noteOn (43);
        out.add (rig.run (4800));
        rig.noteOn (55);
        out.add (rig.run (14400));
        rig.noteOn (36); // and back down
        out.add (rig.run (14400));

        REQUIRE (peak (out.all) > 0.15f);
        REQUIRE (maxStep (out.all) < limit);
    }
}

TEST_CASE ("Cycling the filter mode mid-note does not click", "[clicks][filter]")
{
    auto p = clickPatch (false);
    Rig rig (p);
    Capture out;

    rig.noteOn (43);
    out.add (rig.run (4800));

    for (auto mode : { dsp::FilterMode::lp12, dsp::FilterMode::hp12, dsp::FilterMode::bp12, dsp::FilterMode::lp24, dsp::FilterMode::hp12 })
    {
        p.filter.mode = mode;
        rig.set (p);
        out.add (rig.run (2400));
    }

    REQUIRE (peak (out.all) > 0.3f);
    REQUIRE (maxStep (out.all, 2400) < limit);
}

TEST_CASE ("Switching the amp envelope on and off mid-note does not click", "[clicks][envelope]")
{
    auto p = clickPatch (false);
    p.ampEnv.sustain = 0.5f;
    p.ampEnv.decay = 0.01f;
    Rig rig (p);
    Capture out;

    rig.noteOn (43);
    out.add (rig.run (4800));

    for (bool enabled : { true, false, true })
    {
        p.ampEnv.enabled = enabled;
        rig.set (p);
        out.add (rig.run (4800));
    }

    REQUIRE (peak (out.all) > 0.3f);
    REQUIRE (maxStep (out.all, 2400) < limit);
}

TEST_CASE ("Adding and removing modules mid-note ramps instead of stepping", "[clicks][modules]")
{
    struct Change
    {
        const char* name;
        void (*apply) (EngineParams&);
    };

    const Change changes[] = {
        { "oscillator 2", [] (EngineParams& p) { p.osc[1].enabled = true; p.osc[1].wave = dsp::Wave::sine; p.osc[1].levelDb = -6.0f; } },
        { "oscillator 3", [] (EngineParams& p) { p.osc[2].enabled = true; p.osc[2].wave = dsp::Wave::sine; p.osc[2].levelDb = -6.0f; p.osc[2].octave = -1; } },
        { "filter", [] (EngineParams& p) { p.filter.enabled = true; p.filter.mode = dsp::FilterMode::lp12; p.filter.cutoffHz = 3000.0f; p.filter.resonance = 0.0f; } },
        { "amp envelope", [] (EngineParams& p) { p.ampEnv.enabled = true; p.ampEnv.sustain = 0.5f; p.ampEnv.decay = 0.01f; } },
        { "filter envelope", [] (EngineParams& p)
            {
                p.filter.enabled = true;
                p.filter.cutoffHz = 400.0f;
                p.filter.resonance = 0.0f;
                p.filterEnv.enabled = true;
                p.filterEnv.amount = 1.0f;
                p.filterEnv.attack = 0.5f;
                p.filterEnv.sustain = 1.0f;
            } }
    };

    for (const auto& change : changes)
    {
        auto p = sineParams();
        p.osc[0].levelDb = -6.0f;
        Rig rig (p);
        Capture out;

        rig.noteOn (43);
        out.add (rig.run (4800));

        auto changed = p;
        change.apply (changed);

        rig.set (changed);
        out.add (rig.run (4800));

        // Switch the module off again but leave its settings alone: reverting
        // the whole patch would also change the module's wave mid-fade, and
        // that crossfade is a different behaviour with its own test.
        auto reverted = changed;
        reverted.osc[1].enabled = p.osc[1].enabled;
        reverted.osc[2].enabled = p.osc[2].enabled;
        reverted.filter.enabled = p.filter.enabled;
        reverted.ampEnv.enabled = p.ampEnv.enabled;
        reverted.filterEnv.enabled = p.filterEnv.enabled;
        rig.set (reverted);
        out.add (rig.run (4800));

        INFO (change.name);
        REQUIRE (peak (out.all) > 0.3f);
        REQUIRE (maxStep (out.all, 2400) < 0.02f);
    }
}

TEST_CASE ("Noise fades in and out over 20 ms instead of switching", "[clicks][noise]")
{
    auto p = silentParams();
    p.noise.levelDb = 0.0f;
    p.noise.enabled = false;
    Rig rig (p);

    rig.noteOn (60);
    rig.run (4800);

    p.noise.enabled = true;
    rig.set (p);
    const auto fadeIn = rig.run (4800);
    const double steady = rms (fadeIn, 3840, 960);

    REQUIRE (steady > 0.5);
    REQUIRE (rms (fadeIn, 0, 48) < 0.2 * steady); // the first millisecond is a few percent of full level

    p.noise.enabled = false;
    rig.set (p);
    const auto fadeOut = rig.run (4800);

    REQUIRE (rms (fadeOut, 0, 48) > 0.8 * steady);
    REQUIRE (rms (fadeOut, 3840, 960) < 1.0e-6);
}
