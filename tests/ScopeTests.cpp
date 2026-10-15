#include "EngineRig.h"

using namespace enginetest;

namespace
{
    EngineParams sawThroughFilter (float cutoffHz)
    {
        auto p = sineParams();
        p.osc[0].wave = dsp::Wave::saw;
        p.filter.enabled = true;
        p.filter.mode = dsp::FilterMode::lp24;
        p.filter.cutoffHz = cutoffHz;
        p.filter.resonance = 0.0f;
        p.filter.driveDb = 0.0f;
        return p;
    }

    /** What the scope shows after the note has settled: the last 8,192 samples of `tap`, drained the way the
        editor does (every few thousand samples, so the FIFO never overflows). */
    std::vector<float> scopeAfter (const EngineParams& p, engine::ScopeTap tap, int note)
    {
        Rig rig (p);
        rig.engine.setScopeTap (tap);
        rig.noteOn (note);
        rig.run (9600);

        std::vector<float> shown;
        std::vector<float> chunk (2048);
        for (int i = 0; i < 12; ++i)
        {
            rig.run (2048);
            const int got = rig.engine.readScope (chunk.data(), (int) chunk.size());
            shown.insert (shown.end(), chunk.begin(), chunk.begin() + got);
        }

        REQUIRE (shown.size() >= 8192);
        return std::vector<float> (shown.end() - 8192, shown.end());
    }
}

TEST_CASE ("The scope's spectrum loses its harmonics as the cutoff closes", "[scope][filter]")
{
    const auto open = scopeAfter (sawThroughFilter (8000.0f), engine::ScopeTap::filter, 45);
    const auto closed = scopeAfter (sawThroughFilter (400.0f), engine::ScopeTap::filter, 45);

    // The fundamental (110 Hz) is still there; the harmonics from 2 kHz up have fallen by well over 40 dB.
    REQUIRE (bandLevel (closed, 80.0, 140.0) > 0.5 * bandLevel (open, 80.0, 140.0));
    REQUIRE (bandLevel (closed, 2000.0, 10000.0) < 0.01 * bandLevel (open, 2000.0, 10000.0));
}

TEST_CASE ("The taps listen at different points in the chain", "[scope][filter]")
{
    // The oscillator tap is before the filter, so closing it changes nothing there...
    const auto oscOpen = scopeAfter (sawThroughFilter (8000.0f), engine::ScopeTap::oscillators, 45);
    const auto oscClosed = scopeAfter (sawThroughFilter (400.0f), engine::ScopeTap::oscillators, 45);
    REQUIRE (bandLevel (oscClosed, 2000.0, 10000.0) > 0.9 * bandLevel (oscOpen, 2000.0, 10000.0));

    // ...while the filter and amp taps are after it.
    const auto ampClosed = scopeAfter (sawThroughFilter (400.0f), engine::ScopeTap::amp, 45);
    REQUIRE (bandLevel (ampClosed, 2000.0, 10000.0) < 0.01 * bandLevel (oscClosed, 2000.0, 10000.0));
}
