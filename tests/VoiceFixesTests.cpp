#include "EngineRig.h"

#include "dsp/MultimodeFilter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace enginetest;
using Catch::Approx;
using engine::ModDest;
using engine::ModSource;

namespace
{
    /** Steady-state gain of a filter for a sine at `hz`. */
    double combGain (double sr, float cutoff, float resonance, double hz)
    {
        scratchpad::dsp::MultimodeFilter filter;
        filter.prepare (sr);
        filter.setModeImmediately (scratchpad::dsp::FilterMode::comb);
        filter.setParams (cutoff, resonance, 0.0f);

        const double w = 2.0 * std::numbers::pi * hz / sr;
        const int settle = (int) sr, measure = (int) (0.25 * sr);
        double sumSquares = 0.0;

        for (int n = 0; n < settle + measure; ++n)
        {
            const float y = filter.process ((float) (0.1 * std::sin (w * (double) n)));
            if (n >= settle)
                sumSquares += (double) y * (double) y;
        }

        return std::sqrt (2.0 * sumSquares / (double) measure) / 0.1;
    }

    /** The sub's own band (220 Hz for A4) in a held note. */
    double subBand (float subDb, int unison)
    {
        auto p = sineParams();
        p.osc[0].levelDb = -18.0f;
        p.osc[0].subLevelDb = subDb;
        p.osc[0].unison = unison;
        p.osc[0].unisonDetuneCents = 100.0f;
        p.osc[0].unisonSpread = 0.0f; // mono, so the rig can compare the channels

        Rig rig (p);
        rig.noteOn (69);
        rig.run (9600);
        return bandLevel (rig.run (16384), 200.0, 240.0);
    }
}

TEST_CASE ("The comb filter is tuned to the cutoff to a fraction of a sample, not to a whole one", "[filter][comb]")
{
    // 48 kHz / 2900 Hz is a delay of 16.55 samples. A whole-sample delay would peak at 3000 Hz instead (58 cents sharp).
    const double atTuned = combGain (48000.0, 2900.0f, 1.0f, 2900.0);
    const double atInteger = combGain (48000.0, 2900.0f, 1.0f, 3000.0);

    REQUIRE (atTuned > 0.85);
    REQUIRE (atTuned < 1.2);
    REQUIRE (atInteger < 0.7 * atTuned);
}

TEST_CASE ("The comb filter keeps its peaks at unity and rings no louder on DC at full resonance", "[filter][comb]")
{
    for (const double sr : { 48000.0, 96000.0 })
    {
        scratchpad::dsp::MultimodeFilter filter;
        filter.prepare (sr);
        filter.setModeImmediately (scratchpad::dsp::FilterMode::comb);
        filter.setParams (20.0f, 1.0f, 0.0f); // the longest delay the filter must support, at any sample rate

        float last = 0.0f;
        for (int n = 0; n < (int) (2.0 * sr); ++n)
            last = filter.process (1.0f);

        INFO ("sample rate " << sr);
        REQUIRE (std::abs (last) < 0.02f);
    }
}

TEST_CASE ("The whole engine is small enough to live on a 1 MB Windows stack", "[engine][memory]")
{
    REQUIRE (sizeof (engine::SynthEngine) < 160 * 1024);
}

TEST_CASE ("The sub-oscillator is one centred sine an octave down, however many unison copies the oscillator has", "[oscillator][sub]")
{
    const double single = subBand (0.0f, 1);
    const double stacked = subBand (0.0f, 7);

    REQUIRE (single > 20.0 * subBand (-60.0f, 1)); // present when on, and absent when off
    REQUIRE (stacked == Approx (single).epsilon (0.1)); // seven detuned subs would beat and spread; this one is a single sine
}

TEST_CASE ("Turning the per-voice shaper on or off steps no harder than the shaped wave does", "[shaper][click]")
{
    auto p = sineParams();
    p.osc[0].levelDb = -12.0f;
    p.filter.shaperDriveDb = 24.0f;
    p.filter.shaperEnabled = true;

    Rig rig (p);
    rig.noteOn (57);
    rig.run (9600);
    const auto shaped = rig.run (4800);
    const float normal = maxStep (shaped, 2400);

    p.filter.shaperEnabled = false;
    rig.set (p);
    const auto off = rig.run (4800);

    p.filter.shaperEnabled = true;
    rig.set (p);
    const auto on = rig.run (4800);

    REQUIRE (maxStep (off) < 1.5f * normal);
    REQUIRE (maxStep (on) < 1.5f * normal);
}

TEST_CASE ("A modulated shaper drive does nothing while the shaper is off", "[shaper][modulation]")
{
    auto p = sineParams();
    p.osc[0].levelDb = -12.0f;

    const auto render = [&] (bool connected)
    {
        auto q = p;
        if (connected)
            q.mod[0] = { ModSource::velocity, ModDest::shaperDrive, 1.0f };

        Rig rig (q);
        rig.noteOn (57, 127);
        return rig.run (4800);
    };

    REQUIRE (render (true) == render (false));
}

TEST_CASE ("The per-voice shaper keeps the highs: it is not a 5 kHz low-pass", "[shaper]")
{
    auto p = sineParams();
    p.osc[0].levelDb = -18.0f;
    p.osc[0].wave = dsp::Wave::saw;
    p.filter.shaperEnabled = true;
    p.filter.shaperDriveDb = 6.0f;
    p.filter.shaperMix = 1.0f;

    auto dry = p;
    dry.filter.shaperEnabled = false;

    const auto highs = [] (const EngineParams& q)
    {
        Rig rig (q);
        rig.noteOn (69);
        rig.run (9600);
        return bandLevel (rig.run (16384), 8000.0, 20000.0);
    };

    // A mild shaper adds harmonics; it must not take away what was there above 8 kHz.
    REQUIRE (highs (p) > 0.7 * highs (dry));
}

TEST_CASE ("FM amount modulation reaches only the oscillator FM targets", "[fm][modulation]")
{
    auto p = silentParams();
    p.osc[0].levelDb = -60.0f;
    p.osc[1].enabled = true;
    p.osc[1].levelDb = -60.0f;
    p.osc[2].enabled = true;
    p.osc[2].wave = dsp::Wave::sine;
    p.osc[2].levelDb = -12.0f;
    p.fmTarget = engine::FmTarget::osc2;

    const auto render = [&] (bool connected)
    {
        auto q = p;
        if (connected)
            q.mod[0] = { ModSource::velocity, ModDest::fmAmount, 1.0f };

        Rig rig (q);
        rig.noteOn (69, 127);
        return rig.run (9600);
    };

    REQUIRE (render (true) == render (false));
}

TEST_CASE ("The wavetables are real band-limited tables that morph", "[oscillator][wavetable]")
{
    const auto spectrum = [] (float pulseWidth, int note)
    {
        auto p = sineParams();
        p.osc[0].wave = dsp::Wave::wavetableA;
        p.osc[0].levelDb = -12.0f;
        p.osc[0].pulseWidth = pulseWidth; // on a table this is the morph
        Rig rig (p);
        rig.noteOn (note);
        rig.run (9600);
        return rig.run (16384);
    };

    const auto soft = spectrum (0.05f, 45);
    const auto hard = spectrum (0.95f, 45);

    // Morphing up brings in the upper harmonics.
    REQUIRE (bandLevel (hard, 2000.0, 8000.0) > 5.0 * bandLevel (soft, 2000.0, 8000.0));

    // A high note gets a band-limited table: nothing folds back below the fundamental.
    const auto high = spectrum (0.95f, 96); // about 2.1 kHz
    const double hz = 2093.0;
    REQUIRE (bandLevel (high, 100.0, hz * 0.8) < 0.03 * bandLevel (high, hz * 0.9, hz * 1.1));
}
