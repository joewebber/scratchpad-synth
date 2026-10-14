#include "EngineRig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace enginetest;

namespace
{
    /** Oscillator `index` alone, as a full-level sine. Oscillator 1 stays
        enabled because notes are ignored without it, but it is silent. */
    EngineParams solo (size_t index)
    {
        auto p = silentParams();
        p.osc[index].enabled = true;
        p.osc[index].wave = dsp::Wave::sine;
        p.osc[index].levelDb = 0.0f;
        return p;
    }

    /** Oscillator 1 as a full-level saw into the filter (LP24, no resonance, cutoff 1 kHz). */
    EngineParams sawThroughFilter()
    {
        auto p = sineParams();
        p.osc[0].wave = dsp::Wave::saw;
        p.filter.enabled = true;
        p.filter.mode = dsp::FilterMode::lp24;
        p.filter.cutoffHz = 1000.0f;
        p.filter.resonance = 0.0f;
        p.filter.driveDb = 0.0f;
        return p;
    }

    /** Settled output (a power-of-two window) of `note` through `p`. */
    std::vector<float> settled (const EngineParams& p, int note, int window = 16384)
    {
        Rig rig (p);
        rig.noteOn (note);
        rig.run (9600);
        return rig.run (window);
    }

    EngineParams envelopeParams (float attack, float decay, float sustain, float release)
    {
        auto p = sineParams();
        p.ampEnv.enabled = true;
        p.ampEnv.attack = attack;
        p.ampEnv.decay = decay;
        p.ampEnv.sustain = sustain;
        p.ampEnv.release = release;
        p.ampEnv.velocitySensitivity = 0.0f;
        return p;
    }
}

TEST_CASE ("Each oscillator tunes by octave, semitone and fine within 0.1 cent", "[chain][tuning]")
{
    struct Tuning { int octave; int semitone; float fine; };
    const Tuning tunings[] = {
        { 0, 0, 0.0f },    { 1, 0, 0.0f },   { -2, 0, 0.0f }, { 2, 0, 0.0f },
        { 0, 7, 0.0f },    { 0, -12, 0.0f }, { 0, 0, 37.5f }, { 0, 0, -100.0f },
        { -1, 5, 12.25f }, { 1, -3, 7.0f }
    };

    for (size_t osc = 0; osc < 3; ++osc)
    {
        for (const auto& t : tunings)
        {
            auto p = solo (osc);
            p.osc[osc].octave = t.octave;
            p.osc[osc].semitone = t.semitone;
            p.osc[osc].fineCents = t.fine;

            Rig rig (p);
            rig.noteOn (69);
            rig.run (2400);
            const double hz = measureHz (rig.run (24000));
            const double expected = testutil::midiToHz (69.0 + 12.0 * t.octave + t.semitone + (double) t.fine / 100.0);

            INFO ("osc " << osc + 1 << ", octave " << t.octave << ", semitone " << t.semitone << ", fine " << t.fine);
            REQUIRE (std::abs (testutil::centsBetween (hz, expected)) < 0.1);
        }
    }
}

TEST_CASE ("Oscillator levels add up in dB", "[chain]")
{
    auto p = solo (0);
    p.osc[2].enabled = true;
    p.osc[2].wave = dsp::Wave::sine;
    p.osc[2].levelDb = -12.0f;
    p.osc[2].octave = 1;

    Rig rig (p);
    rig.noteOn (69);
    rig.run (4800);
    const auto x = rig.run (32768);

    const double fundamental = bandLevel (x, 400.0, 480.0);
    const double octave = bandLevel (x, 840.0, 920.0);
    REQUIRE (std::abs (octave / fundamental - std::pow (10.0, -12.0 / 20.0)) < 0.02);
}

TEST_CASE ("Pulse width sets the share of the cycle spent high", "[chain]")
{
    auto p = sineParams();
    p.osc[0].wave = dsp::Wave::pulse;
    p.osc[0].pulseWidth = 0.25f;

    Rig rig (p);
    rig.noteOn (45);
    rig.run (4800);
    const auto x = rig.run (48000);

    const auto high = (double) std::count_if (x.begin(), x.end(), [] (float s) { return s > 0.0f; });
    REQUIRE (std::abs (high / (double) x.size() - 0.25) < 0.01);
}

TEST_CASE ("Phase reset restarts the cycle on every note; without it the phase runs on", "[chain]")
{
    auto twoNotes = [] (bool phaseReset)
    {
        auto p = sineParams();
        p.osc[0].phaseReset = phaseReset;
        Rig rig (p);

        rig.noteOn (69);
        const auto first = rig.run (2000);
        rig.noteOff (69);
        rig.run (600);
        REQUIRE (rig.activeVoices() == 0); // the next note lands on the same, idle voice

        rig.noteOn (69);
        const auto second = rig.run (2000);
        return std::pair { slice (first, 0, 400), slice (second, 0, 400) };
    };

    auto largestDifference = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            d = std::max (d, std::abs (a[i] - b[i]));
        return d;
    };

    const auto [resetA, resetB] = twoNotes (true);
    REQUIRE (largestDifference (resetA, resetB) < 1.0e-4f);

    // The first note left the oscillator about a fifth of a cycle in; the second picks up from there.
    const auto [freeA, freeB] = twoNotes (false);
    REQUIRE (largestDifference (freeA, freeB) > 0.3f);
}

TEST_CASE ("Noise sounds on its own, at its level, and pink leans towards the low end", "[chain][noise]")
{
    auto noiseOnly = [] (bool pink)
    {
        auto p = silentParams();
        p.noise.enabled = true;
        p.noise.pink = pink;
        p.noise.levelDb = 0.0f;
        return p;
    };

    auto lowOverHigh = [] (const std::vector<float>& x) { return bandLevel (x, 50.0, 500.0) / bandLevel (x, 8000.0, 20000.0); };

    const auto white = settled (noiseOnly (false), 60);
    const auto pink = settled (noiseOnly (true), 60);

    // Uniform noise in [-1, 1) has an RMS of 0.577; pink is scaled to match.
    REQUIRE (rms (white, 0, white.size()) > 0.5);
    REQUIRE (rms (white, 0, white.size()) < 0.65);
    REQUIRE (rms (pink, 0, pink.size()) > 0.4);
    REQUIRE (rms (pink, 0, pink.size()) < 0.75);

    REQUIRE (lowOverHigh (pink) > 4.0 * lowOverHigh (white));

    // Noise is its own source: with it off the same patch is silent.
    auto off = noiseOnly (false);
    off.noise.enabled = false;
    REQUIRE (peak (settled (off, 60)) == 0.0f);
}

TEST_CASE ("The filter shapes the noise", "[chain][noise][filter]")
{
    auto p = silentParams();
    p.noise.enabled = true;
    p.noise.levelDb = 0.0f;
    p.filter.mode = dsp::FilterMode::lp24;
    p.filter.cutoffHz = 500.0f;
    p.filter.resonance = 0.0f;

    p.filter.enabled = false;
    const auto raw = settled (p, 60);
    p.filter.enabled = true;
    const auto filtered = settled (p, 60);

    REQUIRE (bandLevel (filtered, 5000.0, 15000.0) < 0.001 * bandLevel (raw, 5000.0, 15000.0));
    REQUIRE (bandLevel (filtered, 50.0, 300.0) > 0.5 * bandLevel (raw, 50.0, 300.0));
}

TEST_CASE ("A bypassed filter does nothing, whatever its settings", "[chain][filter]")
{
    auto a = sawThroughFilter();
    a.filter.enabled = false;
    a.filter.cutoffHz = 200.0f;
    a.filter.resonance = 0.9f;
    a.filter.driveDb = 12.0f;

    auto b = sawThroughFilter();
    b.filter.enabled = false;
    b.filter.cutoffHz = 8000.0f;

    Rig ra (a), rb (b);
    ra.noteOn (45);
    rb.noteOn (45);
    REQUIRE (ra.run (9600) == rb.run (9600));
}

TEST_CASE ("Cutoff and mode decide where the spectrum goes", "[chain][filter]")
{
    auto p = sawThroughFilter();

    auto lowAt = [&] (float cutoff)
    {
        p.filter.mode = dsp::FilterMode::lp24;
        p.filter.cutoffHz = cutoff;
        return settled (p, 45);
    };

    // Raising the cutoff lets the upper harmonics through.
    REQUIRE (bandLevel (lowAt (500.0f), 3000.0, 8000.0) < 0.01 * bandLevel (lowAt (4000.0f), 3000.0, 8000.0));

    p.filter.cutoffHz = 4000.0f;
    p.filter.mode = dsp::FilterMode::lp12;
    const auto lp = settled (p, 45);
    p.filter.mode = dsp::FilterMode::hp12;
    const auto hp = settled (p, 45);

    REQUIRE (bandLevel (hp, 50.0, 400.0) < 0.05 * bandLevel (lp, 50.0, 400.0));
    REQUIRE (bandLevel (hp, 5000.0, 12000.0) > 0.5 * bandLevel (lp, 5000.0, 12000.0)); // HP12 passes what LP12 starts to lose

    // Band-pass at 1 kHz takes both ends down compared with no filter at all.
    auto none = sawThroughFilter();
    none.filter.enabled = false;
    const auto dry = settled (none, 45);

    p.filter.mode = dsp::FilterMode::bp12;
    p.filter.cutoffHz = 1000.0f;
    p.filter.resonance = 0.5f;
    const auto bp = settled (p, 45);

    REQUIRE (bandLevel (bp, 100.0, 300.0) < 0.5 * bandLevel (dry, 100.0, 300.0));
    REQUIRE (bandLevel (bp, 8000.0, 16000.0) < 0.3 * bandLevel (dry, 8000.0, 16000.0));
}

TEST_CASE ("Resonance peaks the response at the cutoff", "[chain][filter]")
{
    auto p = sawThroughFilter();
    p.filter.resonance = 0.0f;
    const auto flat = settled (p, 45);
    p.filter.resonance = 0.9f;
    const auto peaked = settled (p, 45);

    // The 9th to 11th harmonics of a 110 Hz saw sit around the 1 kHz cutoff.
    REQUIRE (bandLevel (peaked, 850.0, 1250.0) > 5.0 * bandLevel (flat, 850.0, 1250.0));
}

TEST_CASE ("Key tracking moves the cutoff with the note", "[chain][filter]")
{
    auto p = sawThroughFilter();
    p.filter.cutoffHz = 1000.0f;

    // Note 84 is two octaves above the middle-C reference: a tracked cutoff is 4 kHz, not 1 kHz.
    p.filter.keyTracking = 0.0f;
    const auto highPlain = settled (p, 84);
    p.filter.keyTracking = 1.0f;
    const auto highTracked = settled (p, 84);
    REQUIRE (bandLevel (highTracked, 4000.0, 12000.0) > 10.0 * bandLevel (highPlain, 4000.0, 12000.0));

    // Note 36 is two octaves below: a tracked cutoff drops to 250 Hz.
    p.filter.keyTracking = 0.0f;
    const auto lowPlain = settled (p, 36);
    p.filter.keyTracking = 1.0f;
    const auto lowTracked = settled (p, 36);
    REQUIRE (bandLevel (lowTracked, 1000.0, 4000.0) < 0.1 * bandLevel (lowPlain, 1000.0, 4000.0));
}

TEST_CASE ("The filter envelope amount moves the cutoff up or down", "[chain][filter][envelope]")
{
    auto p = sawThroughFilter();
    p.filterEnv.enabled = true;
    p.filterEnv.attack = 0.001f;
    p.filterEnv.decay = 0.3f;
    p.filterEnv.sustain = 1.0f;
    p.filterEnv.release = 0.2f;

    // Up to six octaves above 300 Hz at full amount.
    p.filter.cutoffHz = 300.0f;
    p.filterEnv.amount = 0.0f;
    const auto closed = settled (p, 45);
    p.filterEnv.amount = 1.0f;
    const auto open = settled (p, 45);
    REQUIRE (bandLevel (open, 3000.0, 10000.0) > 100.0 * bandLevel (closed, 3000.0, 10000.0));

    // And six octaves below 8 kHz at minus full.
    p.filter.cutoffHz = 8000.0f;
    p.filterEnv.amount = 0.0f;
    const auto bright = settled (p, 45);
    p.filterEnv.amount = -1.0f;
    const auto dark = settled (p, 45);
    REQUIRE (bandLevel (dark, 2000.0, 8000.0) < 0.01 * bandLevel (bright, 2000.0, 8000.0));
}

TEST_CASE ("The filter envelope opens the filter, then closes it as it decays", "[chain][filter][envelope]")
{
    auto p = sawThroughFilter();
    p.filter.cutoffHz = 300.0f;
    p.filterEnv.enabled = true;
    p.filterEnv.attack = 0.001f;
    p.filterEnv.decay = 0.1f;
    p.filterEnv.sustain = 0.0f;
    p.filterEnv.amount = 1.0f;

    Rig rig (p);
    rig.noteOn (45);
    const auto start = rig.run (240 + 2048);
    rig.run (48000);
    const auto later = rig.run (4096);

    const double early = bandLevel (slice (start, 240, 2048), 3000.0, 10000.0);
    const double late = bandLevel (later, 3000.0, 10000.0);
    REQUIRE (early > 20.0 * late);
}

TEST_CASE ("A disabled filter envelope applies no amount", "[chain][filter][envelope]")
{
    auto a = sawThroughFilter();
    a.filterEnv.enabled = false;
    a.filterEnv.amount = 1.0f;

    auto b = sawThroughFilter();
    b.filterEnv.enabled = true;
    b.filterEnv.amount = 0.0f;

    Rig ra (a), rb (b);
    ra.noteOn (45);
    rb.noteOn (45);
    REQUIRE (ra.run (9600) == rb.run (9600));
}

TEST_CASE ("The filter self-oscillates cleanly at full resonance", "[chain][filter]")
{
    for (auto mode : { dsp::FilterMode::lp24, dsp::FilterMode::lp12 })
    {
        auto p = sawThroughFilter();
        p.filter.mode = mode;
        p.filter.cutoffHz = 1000.0f;
        p.filter.resonance = 1.0f;

        Rig rig (p);
        rig.noteOn (45);
        rig.run (48000); // the saw excites the filter

        p.osc[0].levelDb = engine::minLevelDb; // take the input away; the note is still held
        rig.set (p);
        rig.run (14400);
        const auto ring = rig.run (16384);

        INFO ("mode " << (int) mode);
        REQUIRE (std::all_of (ring.begin(), ring.end(), [] (float s) { return std::isfinite (s); }));
        REQUIRE (peak (ring) < 10.0f);
        REQUIRE (rms (ring, 0, ring.size()) > 0.05);

        // A steady tone at the cutoff.
        const double hz = measure (ring, 0, ring.size()).hz;
        REQUIRE (hz > 900.0);
        REQUIRE (hz < 1100.0);
        REQUIRE (std::abs (rms (ring, 0, 8192) / rms (ring, 8192, 8192) - 1.0) < 0.2);
    }
}

TEST_CASE ("The amp envelope shapes the note", "[chain][envelope]")
{
    Rig rig (envelopeParams (0.1f, 0.1f, 0.5f, 0.3f));
    rig.noteOn (69);
    const auto held = rig.run (48000);

    REQUIRE (peakOf (held, 0, 480) < 0.3f);                                    // attack has barely begun
    REQUIRE (peakOf (held, 4320, 960) > 0.9f);                                 // top of the attack, ~100 ms
    REQUIRE (std::abs (peakOf (held, 43200, 2400) - 0.5f) < 0.05f);            // sustain

    rig.noteOff (69);
    const auto released = rig.run (48000);

    REQUIRE (peakOf (released, 960, 480) > 0.15f);   // 20-30 ms after note-off: the 2 ms gate would be long gone
    REQUIRE (peakOf (released, 4800, 480) < 0.1f);   // the tail is on its way down
    REQUIRE (peakOf (released, 24000, 4800) == 0.0f);
    REQUIRE (rig.activeVoices() == 0);
}

TEST_CASE ("Velocity scales the note by the amp envelope's velocity sensitivity", "[chain][envelope]")
{
    auto level = [] (float sensitivity, juce::uint8 velocity, bool envelope)
    {
        auto p = envelopeParams (0.001f, 0.1f, 1.0f, 0.1f);
        p.ampEnv.velocitySensitivity = sensitivity;
        p.ampEnv.enabled = envelope;

        Rig rig (p);
        rig.noteOn (69, velocity);
        rig.run (4800);
        return peak (rig.run (4800));
    };

    const float soft = 64.0f / 127.0f;

    REQUIRE (std::abs (level (1.0f, 64, true) - soft) < 0.01f);
    REQUIRE (std::abs (level (0.5f, 64, true) - (0.5f + 0.5f * soft)) < 0.01f);
    REQUIRE (std::abs (level (0.0f, 64, true) - 1.0f) < 0.01f);
    REQUIRE (std::abs (level (1.0f, 127, true) - 1.0f) < 0.01f);

    // Without the envelope the fixed gate shapes the note, and it ignores velocity.
    REQUIRE (std::abs (level (1.0f, 64, false) - 1.0f) < 0.01f);
}

TEST_CASE ("A voice with a long envelope release ends as soon as the gate takes over", "[chain][envelope]")
{
    auto p = envelopeParams (0.001f, 0.1f, 1.0f, 5.0f);
    Rig rig (p);

    rig.noteOn (69);
    rig.run (4800);
    rig.noteOff (69);
    rig.run (4800);
    REQUIRE (rig.activeVoices() == 1); // still ringing out its 5 s release

    p.ampEnv.enabled = false;
    rig.set (p);
    rig.run (960);
    REQUIRE (rig.activeVoices() == 0);
}
