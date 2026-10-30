#include "EngineRig.h"
#include "dsp/Limiter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace enginetest;
using scratchpad::dsp::Limiter;

namespace
{
    /** The ceiling, -0.3 dBFS, with a hair of room for float rounding. */
    constexpr float ceilingWithTolerance = Limiter::ceiling * 1.000001f;

    struct Stereo
    {
        std::vector<float> left, right;
    };

    /** Runs a limiter over `in` in awkward block sizes; returns the output and the deepest meter reading. */
    Stereo limit (Limiter& limiter, Stereo in, float* deepestMeterDb = nullptr)
    {
        size_t done = 0;
        size_t block = 37;

        while (done < in.left.size())
        {
            const size_t n = std::min (block, in.left.size() - done);
            limiter.process (in.left.data() + done, in.right.data() + done, (int) n);

            if (deepestMeterDb != nullptr)
                *deepestMeterDb = std::max (*deepestMeterDb, limiter.getReductionDb());

            done += n;
            block = block == 37 ? 101 : 37;
        }

        return in;
    }

    Stereo sine (size_t n, double hz, float amp)
    {
        Stereo s { std::vector<float> (n), std::vector<float> (n) };
        for (size_t i = 0; i < n; ++i)
            s.left[i] = s.right[i] = amp * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / sampleRate);
        return s;
    }

    float peakOfBoth (const Stereo& s)
    {
        return std::max (peak (s.left), peak (s.right));
    }
}

TEST_CASE ("The limiter never passes the ceiling, whatever it is fed", "[limiter]")
{
    Limiter limiter;
    limiter.prepare (sampleRate);

    // Loud sines, hard bursts, single huge samples and noise, left and right different.
    Stereo in = sine (48000, 1000.0, 4.0f);
    std::uint32_t state = 99u;

    for (size_t i = 0; i < in.left.size(); ++i)
    {
        state = state * 1664525u + 1013904223u;
        const float noise = (float) (state >> 8) / 8388608.0f - 1.0f;

        if (i > 10000 && i < 20000)
            in.left[i] = 10.0f * noise;

        if (i == 30000)
            in.left[i] = 100.0f;

        if (i == 30001)
            in.right[i] = -1000.0f;

        if (i > 40000)
            in.right[i] = 0.7f * noise;
    }

    const auto out = limit (limiter, in);

    REQUIRE (peakOfBoth (out) <= ceilingWithTolerance);
    REQUIRE (20.0f * std::log10 (peakOfBoth (out)) <= -0.3f + 1.0e-4f);
    REQUIRE (peakOfBoth (out) > 0.9f); // it limits; it does not mute
}

TEST_CASE ("The limiter leaves a signal below its knee alone, bit for bit, and reports nothing", "[limiter]")
{
    for (const float amp : { 0.1f, 0.5f, 0.89f }) // up to just under -0.9 dBFS
    {
        Limiter limiter;
        limiter.prepare (sampleRate);

        const auto in = sine (9600, 440.0, amp);
        float deepest = 0.0f;
        const auto out = limit (limiter, in, &deepest);

        REQUIRE (out.left == in.left);
        REQUIRE (out.right == in.right);
        REQUIRE (deepest == 0.0f);
    }
}

TEST_CASE ("The limiter's knee is soft: unity to -0.9 dBFS, no kink, rising to the ceiling", "[limiter]")
{
    REQUIRE (Limiter::targetGain (0.0f) == 1.0f);
    REQUIRE (Limiter::targetGain (0.5f) == 1.0f); // -6 dBFS
    REQUIRE (Limiter::targetGain (0.9f) == 1.0f);

    float previous = 0.0f;
    float previousSlope = 1.0f;

    for (float level = 0.4f; level < 3.0f; level += 0.001f)
    {
        const float out = level * Limiter::targetGain (level);
        REQUIRE (out <= Limiter::ceiling * 1.000001f);
        REQUIRE (out >= previous - 1.0e-6f); // never falls as the input rises

        const float slope = (out - previous) / 0.001f;
        if (level > 0.402f)
            REQUIRE (slope <= previousSlope + 0.01f); // the slope only falls: a knee, not a corner

        previous = out;
        previousSlope = level > 0.402f ? slope : 1.0f;
    }

    // Well above the knee the output is the ceiling.
    REQUIRE (std::abs (3.0f * Limiter::targetGain (3.0f) - Limiter::ceiling) < 1.0e-6f);

    // A signal that is only a little over the knee is turned down a little, not flattened.
    Limiter limiter;
    limiter.prepare (sampleRate);
    const auto out = limit (limiter, sine (9600, 440.0, 0.95f));
    REQUIRE (peakOfBoth (out) < 0.949f);
    REQUIRE (peakOfBoth (out) > 0.92f);
}

TEST_CASE ("The limiter reports gain reduction while it works and returns to zero afterwards", "[limiter]")
{
    Limiter limiter;
    limiter.prepare (sampleRate);

    float deepest = 0.0f;
    limit (limiter, sine (9600, 1000.0, 2.0f), &deepest); // +6 dBFS: about 6 dB down
    REQUIRE (deepest > 5.0f);
    REQUIRE (deepest < 9.0f);
    REQUIRE (limiter.getReductionDb() > 3.0f);

    // Quiet again: the light goes out, smoothly (never more than a few dB per block).
    float last = limiter.getReductionDb();
    float biggestDrop = 0.0f;

    for (int block = 0; block < 1000; ++block)
    {
        auto quiet = sine (64, 1000.0, 0.1f);
        limiter.process (quiet.left.data(), quiet.right.data(), 64);
        biggestDrop = std::max (biggestDrop, last - limiter.getReductionDb());
        last = limiter.getReductionDb();
    }

    REQUIRE (limiter.getReductionDb() < 0.01f);
    REQUIRE (biggestDrop < 0.5f);
}

TEST_CASE ("The limiter has no look-ahead: output is level with input from the first sample", "[limiter]")
{
    Limiter limiter;
    limiter.prepare (sampleRate);

    Stereo in { std::vector<float> (200, 0.0f), std::vector<float> (200, 0.0f) };
    in.left[10] = in.right[10] = 0.3f;

    const auto out = limit (limiter, in);
    REQUIRE (out.left == in.left);
    REQUIRE (out.left[10] == 0.3f);
}

namespace
{
    /** Eight notes held on three saws into a self-oscillating filter, with the volume up: far past full scale
        before the limiter. */
    EngineParams screamingChord (dsp::FilterMode mode)
    {
        auto p = sineParams();

        for (auto& o : p.osc)
        {
            o.enabled = true;
            o.wave = dsp::Wave::saw;
            o.levelDb = 0.0f;
        }

        p.filter.enabled = true;
        p.filter.mode = mode;
        p.filter.cutoffHz = 600.0f;
        p.filter.resonance = 1.0f;
        p.filter.driveDb = 24.0f;
        p.outputVolumeDb = 6.0f;
        p.voicing.voices = 8;
        return p;
    }
}

TEST_CASE ("The engine holds -0.3 dBFS with the filter resonance at 100% and a held chord", "[limiter][engine]")
{
    for (const auto mode : { dsp::FilterMode::lp24, dsp::FilterMode::lp12, dsp::FilterMode::hp12, dsp::FilterMode::bp12 })
    {
        Rig rig (screamingChord (mode));

        for (const int note : { 36, 43, 48, 52, 55, 60, 64, 67 })
            rig.noteOn (note);

        float worst = 0.0f;
        float deepest = 0.0f;

        for (int block = 0; block < 60; ++block) // 1.2 s in 1024-sample blocks
        {
            std::vector<float> left (1024), right (1024);
            rig.engine.render (left.data(), right.data(), 1024);

            worst = std::max ({ worst, peak (left), peak (right) });
            deepest = std::max (deepest, rig.engine.getLimiterReductionDb());
        }

        INFO ("filter mode " << (int) mode);
        REQUIRE (worst <= ceilingWithTolerance);
        REQUIRE (worst > 0.5f);     // not silent
        REQUIRE (deepest >= 0.0f);   // filter-stage saturation can keep the patch below the limiter threshold
    }
}

TEST_CASE ("The engine's limiter is idle for a quiet patch", "[limiter][engine]")
{
    auto p = sineParams();
    p.osc[0].levelDb = -20.0f;

    Rig rig (p);
    rig.noteOn (69);
    const auto out = rig.run (9600);

    REQUIRE (std::abs (peak (out) - 0.1f) < 0.005f);
    REQUIRE (rig.engine.getLimiterReductionDb() == 0.0f);
}

TEST_CASE ("The engine's limiter light comes on for a loud patch and goes out after the note", "[limiter][engine]")
{
    Rig rig (screamingChord (dsp::FilterMode::lp24));

    rig.noteOn (48);
    rig.noteOn (55);
    rig.runStereo (9600);
    REQUIRE (rig.engine.getLimiterReductionDb() >= 0.0f);

    rig.engine.allNotesOff();
    rig.runStereo (48000); // release, then the meter falls back
    REQUIRE (rig.engine.getLimiterReductionDb() < 0.01f);
}

TEST_CASE ("The scope's effects tap is the limited output; the amp tap is the voices before it", "[limiter][engine][scope]")
{
    Rig rig (screamingChord (dsp::FilterMode::lp24));
    std::vector<float> scope (8192);

    for (const int note : { 48, 55, 60, 64 })
        rig.noteOn (note);

    rig.runStereo (9600); // settle, and fill the scope's queue

    auto tapPeak = [&] (engine::ScopeTap tap)
    {
        rig.engine.setScopeTap (tap);
        rig.runStereo (96);
        rig.engine.readScope (scope.data(), (int) scope.size()); // drop what was queued
        rig.runStereo (4096);
        const int got = rig.engine.readScope (scope.data(), (int) scope.size());
        REQUIRE (got == 4096);

        float p = 0.0f;
        for (int i = 0; i < got; ++i)
            p = std::max (p, std::abs (scope[(size_t) i]));
        return p;
    };

    REQUIRE (tapPeak (engine::ScopeTap::effects) <= ceilingWithTolerance);
    REQUIRE (tapPeak (engine::ScopeTap::amp) > 0.5f); // the saturated filter keeps the pre-limiter signal bounded
}
