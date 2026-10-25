#include "EngineRig.h"
#include "engine/EffectsChain.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace enginetest;
using engine::EffectKind;
using engine::EffectsChain;

namespace
{
    std::vector<float> sineWave (size_t n, double hz, float amp)
    {
        std::vector<float> x (n);
        for (size_t i = 0; i < n; ++i)
            x[i] = amp * (float) std::sin (2.0 * std::numbers::pi * hz * (double) i / sampleRate);
        return x;
    }

    /** A sine with deterministic noise on top, so nothing about it is tidy. */
    std::vector<float> busySignal (size_t n)
    {
        auto x = sineWave (n, 440.0, 0.3f);
        std::uint32_t state = 12345u;
        for (auto& s : x)
        {
            state = state * 1664525u + 1013904223u;
            s += 0.1f * ((float) (state >> 8) / 8388608.0f - 1.0f);
        }
        return x;
    }

    std::vector<float> part (const std::vector<float>& x, size_t from, size_t count)
    {
        return std::vector<float> (x.begin() + (std::ptrdiff_t) from, x.begin() + (std::ptrdiff_t) (from + count));
    }

    float stepsIn (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    float peakIn (const std::vector<float>& x, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = from; i < to; ++i)
            m = std::max (m, std::abs (x[i]));
        return m;
    }

    /** An `EffectsChain` driven in awkward block sizes; keeps everything it has produced. */
    struct ChainRun
    {
        explicit ChainRun (const EngineParams& p)
        {
            chain.setParams (p); // before prepare: effects that are on start fully on
            chain.prepare (sampleRate);
        }

        void set (const EngineParams& p) { chain.setParams (p); }

        void process (std::vector<float> l, std::vector<float> r)
        {
            size_t done = 0;
            size_t block = 50;

            while (done < l.size())
            {
                const size_t n = std::min (block, l.size() - done);
                chain.process (l.data() + done, r.data() + done, (int) n);
                done += n;
                block = block == 50 ? 77 : 50;
            }

            left.insert (left.end(), l.begin(), l.end());
            right.insert (right.end(), r.begin(), r.end());
        }

        void process (const std::vector<float>& mono) { process (mono, mono); }

        EffectsChain chain;
        std::vector<float> left, right;
    };

    EngineParams delayOnly (float timeMs, float feedback, bool pingPong, float mix)
    {
        EngineParams p;
        p.delay.enabled = true;
        p.delay.timeMs = timeMs;
        p.delay.feedback = feedback;
        p.delay.pingPong = pingPong;
        p.delay.mix = mix;
        return p;
    }

    /** A 200 Hz Hann-windowed burst, 480 samples long, at the start of `n` samples of silence. */
    std::vector<float> burst (size_t n, float amp = 0.5f)
    {
        std::vector<float> x (n, 0.0f);
        for (size_t i = 0; i < 480; ++i)
            x[i] = amp * (float) (std::sin (2.0 * std::numbers::pi * 200.0 * (double) i / sampleRate)
                                  * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * (double) i / 480.0)));
        return x;
    }

    /** Reverberation time from the Schroeder backward integral: the time between -5 and -25 dB, times three. */
    double estimateRt60 (const std::vector<float>& x)
    {
        std::vector<double> edc (x.size());
        double acc = 0.0;
        for (size_t i = x.size(); i-- > 0;)
        {
            acc += (double) x[i] * (double) x[i];
            edc[i] = acc;
        }

        auto timeAt = [&] (double db)
        {
            const double target = edc[0] * std::pow (10.0, db / 10.0);
            for (size_t i = 0; i < edc.size(); ++i)
                if (edc[i] <= target)
                    return (double) i / sampleRate;
            return -1.0;
        };

        const double t5 = timeAt (-5.0);
        const double t25 = timeAt (-25.0);
        REQUIRE (t5 >= 0.0);
        REQUIRE (t25 > t5);
        return 3.0 * (t25 - t5);
    }

    /** An impulse through a one-pole low-pass (about 800 Hz), so the tail is bass the interpolators don't touch. */
    std::vector<float> softImpulse (size_t n)
    {
        std::vector<float> x (n, 0.0f);
        float state = 0.0f;
        for (size_t i = 0; i < 2000; ++i)
        {
            state += 0.0994f * ((i == 0 ? 1.0f : 0.0f) - state);
            x[i] = state;
        }
        return x;
    }

    EngineParams reverbOnly (float size, float decay, float damping, float mix)
    {
        EngineParams p;
        p.reverb.enabled = true;
        p.reverb.size = size;
        p.reverb.decaySeconds = decay;
        p.reverb.damping = damping;
        p.reverb.mix = mix;
        return p;
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            ab += (double) a[i] * (double) b[i];
            aa += (double) a[i] * (double) a[i];
            bb += (double) b[i] * (double) b[i];
        }
        return ab / std::sqrt (aa * bb);
    }

    /** Folds a frequency above Nyquist back into 0 .. Nyquist, as sampling does. */
    double folded (double hz)
    {
        hz = std::fmod (hz, sampleRate);
        return hz > 0.5 * sampleRate ? sampleRate - hz : hz;
    }

    /** A patch with one effect on, set so it does plenty: a good subject for click tests. */
    EngineParams oneEffect (EffectKind kind)
    {
        EngineParams p;
        switch (kind)
        {
            case EffectKind::distortion:
                p.distortion.enabled = true;
                p.distortion.driveDb = 12.0f;
                p.distortion.mix = 1.0f;
                break;
            case EffectKind::chorus:
                p.chorus.enabled = true;
                p.chorus.depth = 1.0f;
                p.chorus.mix = 0.5f;
                break;
            case EffectKind::delay:
                p.delay = delayOnly (100.0f, 0.4f, false, 0.5f).delay;
                break;
            case EffectKind::reverb:
                p.reverb = reverbOnly (0.5f, 2.0f, 0.5f, 0.5f).reverb;
                break;
        }
        return p;
    }

    EngineParams orderTestParams()
    {
        EngineParams p;
        p.distortion.enabled = true;
        p.distortion.driveDb = 20.0f;
        p.distortion.mix = 1.0f;
        p.delay = delayOnly (100.0f, 0.5f, false, 0.5f).delay;
        return p;
    }
}

TEST_CASE ("Every effect at mix 0 passes the signal through unchanged", "[effects]")
{
    const auto in = busySignal (9600);

    for (int k = 0; k < engine::numEffects; ++k)
    {
        const auto kind = (EffectKind) k;
        auto p = oneEffect (kind);
        p.distortion.mix = 0.0f;
        p.chorus.mix = 0.0f;
        p.delay.mix = 0.0f;
        p.reverb.mix = 0.0f;

        ChainRun run (p);
        run.process (in);

        // Distortion's dry path is late by the oversampler's latency, to line up with its wet path.
        const size_t latency = kind == EffectKind::distortion ? (size_t) run.chain.getLatencySamples() : 0;

        float worst = 0.0f;
        for (size_t i = latency; i < in.size(); ++i)
        {
            worst = std::max (worst, std::abs (run.left[i] - in[i - latency]));
            worst = std::max (worst, std::abs (run.right[i] - in[i - latency]));
        }

        INFO ("effect " << k);
        REQUIRE (worst <= 1.0e-6f); // -120 dB
    }
}

TEST_CASE ("The effects latency is a constant, reported with distortion off, and the dry path of a mix is aligned to it", "[effects][latency]")
{
    Rig off;
    const int latency = off.engine.getEffectsLatencySamples();
    REQUIRE (latency > 0);
    REQUIRE (latency < 200);

    auto p = sineParams();
    p.distortion.enabled = true;
    Rig on (p);
    REQUIRE (on.engine.getEffectsLatencySamples() == latency);

    // Half dry, half wet at 0 dB of drive on a quiet 1 kHz sine: the two halves are the same signal only if
    // the dry half has been delayed by the same amount as the wet half.
    EngineParams q;
    q.distortion.enabled = true;
    q.distortion.driveDb = 0.0f;
    q.distortion.tone = 1.0f;
    q.distortion.mix = 0.5f;

    const auto in = sineWave (9600, 1000.0, 0.02f);
    ChainRun run (q);
    REQUIRE (run.chain.getLatencySamples() == latency);
    run.process (in);

    float worst = 0.0f;
    for (size_t i = 1000; i < in.size(); ++i)
        worst = std::max (worst, std::abs (run.left[i] - in[i - (size_t) latency]));

    REQUIRE (worst < 0.04f * 0.02f);
}

TEST_CASE ("The delay's echoes arrive at the set time and fall by the feedback", "[effects][delay]")
{
    for (const float timeMs : { 100.0f, 250.0f })
    {
        const size_t period = (size_t) std::lround (timeMs * 0.001 * sampleRate);
        const auto in = burst (period * 5 + 2000);


        ChainRun run (delayOnly (timeMs, 0.5f, false, 1.0f));
        run.process (in);

        INFO ("time " << timeMs);
        REQUIRE (peakIn (run.left, 0, period - 10) == 0.0f); // mix 1: nothing before the first echo

        std::array<float, 4> echo {};
        for (size_t n = 1; n <= 4; ++n)
        {
            echo[n - 1] = peakIn (run.left, n * period - 10, n * period + 520);
            if (n == 1)
            {
                // The first echo is the input, exactly `period` samples later.
                for (size_t i = 0; i < 480; ++i)
                    REQUIRE (std::abs (run.left[i + period] - in[i]) < 1.0e-4f);
            }
        }

        // The first echo is the burst at the feedback-free gain of the mix (1.0); the burst's own peak is
        // below its nominal amplitude because the sine and the window peak at different places.
        REQUIRE (std::abs (echo[0] - peakIn (in, 0, 480)) < 1.0e-4f);

        for (size_t n = 1; n < 4; ++n)
            REQUIRE (std::abs (echo[n] / echo[n - 1] - 0.5f) < 0.04f);
    }
}

TEST_CASE ("Ping-pong sends the echoes left, right, left; without it each channel keeps its own", "[effects][delay]")
{
    constexpr size_t period = 4800;
    const auto in = burst (period * 4 + 1000);

    {
        ChainRun run (delayOnly (100.0f, 0.6f, true, 1.0f));
        run.process (in);

        auto level = [&] (const std::vector<float>& x, size_t n) { return peakIn (x, n * period - 10, n * period + 520); };

        REQUIRE (level (run.left, 1) > 0.4f);
        REQUIRE (level (run.right, 1) < 1.0e-4f);
        REQUIRE (level (run.right, 2) > 0.2f);
        REQUIRE (level (run.left, 2) < 1.0e-4f);
        REQUIRE (level (run.left, 3) > 0.1f);
        REQUIRE (level (run.right, 3) < 1.0e-4f);
    }

    {
        ChainRun run (delayOnly (100.0f, 0.6f, false, 1.0f));
        run.process (in, std::vector<float> (in.size(), 0.0f)); // left only

        REQUIRE (peakIn (run.left, period - 10, period + 520) > 0.4f);
        REQUIRE (peakIn (run.right, 0, in.size()) == 0.0f);
    }
}

TEST_CASE ("The reverb tail falls 60 dB in about the decay time, at any decay", "[effects][reverb]")
{
    std::array<double, 2> measured {};
    const std::array<float, 2> decays { 0.5f, 2.0f };

    for (size_t k = 0; k < 2; ++k)
    {
        ChainRun run (reverbOnly (0.5f, decays[k], 0.0f, 1.0f));
        run.process (softImpulse (size_t (decays[k] * 5.0f * (float) sampleRate)));

        measured[k] = estimateRt60 (run.left);
        INFO ("decay " << decays[k] << " measured " << measured[k]);
        REQUIRE (measured[k] > 0.75 * (double) decays[k]);
        REQUIRE (measured[k] < 1.3 * (double) decays[k]);
    }

    REQUIRE (measured[1] / measured[0] > 3.0);
    REQUIRE (measured[1] / measured[0] < 5.2);
}

TEST_CASE ("The reverb tail is decorrelated between the ears, and damping takes the treble out of it", "[effects][reverb]")
{
    std::array<double, 2> trebleToBass {};

    for (size_t k = 0; k < 2; ++k)
    {
        std::vector<float> impulse (96000, 0.0f);
        impulse[0] = 1.0f;

        ChainRun run (reverbOnly (0.5f, 3.0f, k == 0 ? 0.0f : 1.0f, 1.0f));
        run.process (impulse); // the same in both channels

        if (k == 0)
            REQUIRE (std::abs (correlation (run.left, run.right, 4800, 48000)) < 0.3);

        // A power-of-two window of the tail, after the first 100 ms.
        const auto tail = part (run.left, 4800, 16384);
        trebleToBass[k] = bandLevel (tail, 6000.0, 20000.0) / bandLevel (tail, 100.0, 1500.0);
    }

    REQUIRE (trebleToBass[1] < 0.3 * trebleToBass[0]);
}

TEST_CASE ("Bigger rooms are later: size moves the first reflections", "[effects][reverb]")
{
    // Energy in the first 20 ms of the response: a small room has already answered, a big one has not.
    auto early = [] (float size)
    {
        std::vector<float> impulse (4800, 0.0f);
        impulse[0] = 1.0f;
        ChainRun run (reverbOnly (size, 2.0f, 0.0f, 1.0f));
        run.process (impulse);
        return rms (run.left, 0, 960);
    };

    REQUIRE (early (0.0f) > 2.0 * early (1.0f));
}

TEST_CASE ("The chorus moves the sound, differently in each ear, and mix 0 does not", "[effects][chorus]")
{
    const auto in = sineWave (48000, 440.0, 0.4f);

    EngineParams p;
    p.chorus.enabled = true;
    p.chorus.rateHz = 2.0f;
    p.chorus.depth = 1.0f;
    p.chorus.mix = 0.5f;

    ChainRun run (p);
    run.process (in);

    double difference = 0.0, ears = 0.0;
    for (size_t i = 12000; i < in.size(); ++i)
    {
        difference = std::max (difference, (double) std::abs (run.left[i] - in[i]));
        ears = std::max (ears, (double) std::abs (run.left[i] - run.right[i]));
    }

    REQUIRE (difference > 0.1);
    REQUIRE (ears > 0.1);

    // The level stays sensible: two taps at half weight, half dry.
    REQUIRE (peakIn (run.left, 12000, in.size()) < 0.45f);
}

TEST_CASE ("Distortion adds harmonics that were not there", "[effects][distortion]")
{
    // 1 kHz on a bin centre of a 16384-point window at 48 kHz.
    const double f0 = 1000.0;
    const auto in = sineWave (16384 + 2000, f0, 0.5f);

    EngineParams p;
    p.distortion.enabled = true;
    p.distortion.driveDb = 20.0f;
    p.distortion.tone = 1.0f;
    p.distortion.mix = 1.0f;

    ChainRun run (p);
    run.process (in);

    const auto clean = part (in, 2000, 16384);
    const auto dirty = part (run.left, 2000, 16384);

    const double cleanThird = bandLevel (clean, 2900.0, 3100.0) / bandLevel (clean, 900.0, 1100.0);
    const double dirtyThird = bandLevel (dirty, 2900.0, 3100.0) / bandLevel (dirty, 900.0, 1100.0);
    const double dirtyEven = bandLevel (dirty, 1900.0, 2100.0) / bandLevel (dirty, 900.0, 1100.0);

    REQUIRE (cleanThird < 1.0e-3);
    REQUIRE (dirtyThird > 0.1);       // a squashed sine has a third harmonic only a few dB down
    REQUIRE (dirtyEven < dirtyThird); // tanh is symmetric: odd harmonics only
}

TEST_CASE ("Oversampling keeps distortion's aliasing well below the naive waveshaper's", "[effects][distortion]")
{
    constexpr size_t n = 16384;
    const double f0 = 1707.0 * sampleRate / (double) n; // a bin centre, near 5 kHz
    const float driveDb = 24.0f;

    std::vector<float> in (n + 4000);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = 0.5f * (float) std::sin (2.0 * std::numbers::pi * f0 * (double) i / sampleRate);

    EngineParams p;
    p.distortion.enabled = true;
    p.distortion.driveDb = driveDb;
    p.distortion.tone = 1.0f;
    p.distortion.mix = 1.0f;

    ChainRun run (p);
    run.process (in);
    const auto oversampled = part (run.left, 4000, n);

    // The same shaper with no oversampling.
    const float gain = std::pow (10.0f, driveDb / 20.0f);
    const float makeup = std::pow (gain, -0.25f);
    std::vector<float> naive (n);
    for (size_t i = 0; i < n; ++i)
        naive[i] = std::tanh (in[i + 4000] * gain) * makeup;

    // The 7th, 9th, 11th and 13th harmonics are above Nyquist, and fold to 13, 3, 7 and 17 kHz, where
    // the signal has no harmonic of its own.
    double aliasedOversampled = 0.0, aliasedNaive = 0.0;
    for (const int m : { 7, 9, 11, 13 })
    {
        const double hz = folded ((double) m * f0);
        aliasedOversampled += bandLevel (oversampled, hz - 30.0, hz + 30.0);
        aliasedNaive += bandLevel (naive, hz - 30.0, hz + 30.0);
    }

    const double fundamental = bandLevel (naive, f0 - 30.0, f0 + 30.0);

    INFO ("naive " << aliasedNaive / fundamental << " oversampled " << aliasedOversampled / fundamental);
    REQUIRE (aliasedNaive / fundamental > 0.01); // the naive one really does alias
    REQUIRE (aliasedOversampled < 0.1 * aliasedNaive); // at least 20 dB better
}

TEST_CASE ("The order of the effects is audible", "[effects][order]")
{
    const auto a = orderTestParams();
    auto b = a;
    b.effectOrder = { EffectKind::delay, EffectKind::chorus, EffectKind::distortion, EffectKind::reverb };

    // A burst shorter than the delay, so the dry signal and its echo do not overlap.
    auto in = sineWave (3000, 220.0, 0.3f);
    in.resize (48000, 0.0f);

    ChainRun first (a), second (b);
    first.process (in);
    second.process (in);

    double difference = 0.0, energy = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        difference += std::pow ((double) first.left[i] - (double) second.left[i], 2.0);
        energy += std::pow ((double) first.left[i], 2.0);
    }

    REQUIRE (energy > 0.0);
    REQUIRE (std::sqrt (difference / energy) > 0.1);
}

TEST_CASE ("Reordering effects mid-signal does not click, and lands on the new order", "[effects][order][glitch]")
{
    auto a = orderTestParams();
    a.distortion.driveDb = 6.0f;
    auto b = a;
    b.effectOrder = { EffectKind::delay, EffectKind::chorus, EffectKind::distortion, EffectKind::reverb };

    constexpr size_t half = 48000;
    const auto in = sineWave (2 * half, 220.0, 0.3f);

    auto steady = [&] (const EngineParams& p)
    {
        ChainRun run (p);
        run.process (in);
        return run.left;
    };

    const auto steadyA = steady (a);
    const auto steadyB = steady (b);
    const float bound = 1.3f * std::max (stepsIn (steadyA, half, 2 * half), stepsIn (steadyB, half, 2 * half)) + 0.01f;

    ChainRun run (a);
    run.process (part (in, 0, half));
    run.set (b);
    run.process (part (in, half, half));

    // The swap itself, and the fades round it: no step beyond what the chain makes at rest. (Later, at one
    // delay time after the swap, the delay line replays what it held before the swap: the old order's
    // signal, a little out of step with the new order's by the distortion's latency. That is the tail
    // of the old order still sounding, not a click of the swap, so the window stops short of it.)
    REQUIRE (stepsIn (run.left, half - 1000, half + 4000) <= bound);

    // A second half-second on, the chain is in the new order: it matches a chain that started there.
    double worst = 0.0;
    for (size_t i = 2 * half - 4000; i < 2 * half; ++i)
        worst = std::max (worst, (double) std::abs (run.left[i] - steadyB[i]));

    REQUIRE (worst < 0.02);
}

TEST_CASE ("Reordering effects that are off changes nothing", "[effects][order]")
{
    EngineParams a;
    a.delay = delayOnly (100.0f, 0.4f, false, 0.5f).delay;

    auto b = a;
    b.effectOrder = { EffectKind::reverb, EffectKind::chorus, EffectKind::delay, EffectKind::distortion };

    const auto in = sineWave (24000, 220.0, 0.3f);

    ChainRun first (a);
    first.process (in);

    ChainRun second (a);
    second.process (part (in, 0, 12000));
    second.set (b);
    second.process (part (in, 12000, 12000));

    REQUIRE (first.left == second.left);
}

TEST_CASE ("Switching an effect on or off mid-stream does not click", "[effects][glitch]")
{
    constexpr size_t at = 24000, total = 72000;
    const auto in = sineWave (total, 110.0, 0.3f);

    for (int k = 0; k < engine::numEffects; ++k)
    {
        const auto kind = (EffectKind) k;
        const auto on = oneEffect (kind);
        const EngineParams off {};

        ChainRun steady (on);
        steady.process (in);
        const float bound = 1.3f * stepsIn (steady.left, at - 1000, total) + 0.01f;

        INFO ("effect " << k);

        {
            ChainRun run (off);
            run.process (part (in, 0, at));
            run.set (on);
            run.process (part (in, at, total - at));

            REQUIRE (stepsIn (run.left, at - 1000, total) <= bound);
            REQUIRE (peakIn (run.left, at, total) > 0.0f);
            REQUIRE (! run.chain.isIdle());
        }

        {
            ChainRun run (on);
            run.process (part (in, 0, at));
            run.set (off);
            run.process (part (in, at, total - at));

            REQUIRE (stepsIn (run.left, at - 1000, total) <= bound);
        }
    }
}

TEST_CASE ("A switched-off effect fades, is then skipped, and comes back empty", "[effects]")
{
    const auto p = delayOnly (100.0f, 0.7f, false, 0.8f);
    const EngineParams off {};

    ChainRun run (p);
    run.process (burst (12000));
    REQUIRE (! run.chain.isIdle());

    // Off with echoes still in the line: they fade over 15 ms rather than being cut, then nothing runs.
    run.set (off);
    REQUIRE (! run.chain.isIdle());
    run.process (std::vector<float> (2000, 0.0f));
    REQUIRE (run.chain.isIdle());

    // On again with a silent input: no echo of the old signal comes out.
    run.set (p);
    const size_t before = run.left.size();
    run.process (std::vector<float> (24000, 0.0f));
    REQUIRE (peakIn (run.left, before, run.left.size()) == 0.0f);
}

TEST_CASE ("Parameter changes glide: no step in the output when a knob jumps", "[effects][glitch]")
{
    constexpr size_t at = 24000, total = 60000;
    const auto in = sineWave (total, 110.0, 0.3f);

    auto jump = [&] (EngineParams before, EngineParams after, float bound)
    {
        ChainRun run (before);
        run.process (part (in, 0, at));
        run.set (after);
        run.process (part (in, at, total - at));
        REQUIRE (stepsIn (run.left, at - 500, total) <= bound);
        REQUIRE (stepsIn (run.right, at - 500, total) <= bound);
    };

    SECTION ("delay time") { jump (delayOnly (100.0f, 0.4f, false, 0.5f), delayOnly (700.0f, 0.4f, false, 0.5f), 0.06f); }
    SECTION ("delay feedback and mix") { jump (delayOnly (100.0f, 0.1f, false, 0.1f), delayOnly (100.0f, 0.9f, true, 1.0f), 0.06f); }
    SECTION ("reverb size and decay") { jump (reverbOnly (0.1f, 0.3f, 0.1f, 0.5f), reverbOnly (1.0f, 8.0f, 0.9f, 0.9f), 0.08f); }

    SECTION ("chorus rate and depth")
    {
        auto a = oneEffect (EffectKind::chorus);
        a.chorus.depth = 0.0f;
        a.chorus.rateHz = 0.2f;
        auto b = a;
        b.chorus.depth = 1.0f;
        b.chorus.rateHz = 8.0f;
        b.chorus.mix = 1.0f;
        jump (a, b, 0.06f);
    }

    SECTION ("distortion drive and mix")
    {
        auto a = oneEffect (EffectKind::distortion);
        a.distortion.driveDb = 0.0f;
        a.distortion.mix = 0.0f;
        auto b = a;
        b.distortion.driveDb = 40.0f;
        b.distortion.mix = 1.0f;
        b.distortion.tone = 0.1f;

        // The steady output of the loud setting is the yardstick: a step bigger than its own edges is a click.
        ChainRun steady (b);
        steady.process (in);
        jump (a, b, 1.3f * stepsIn (steady.left, at, total) + 0.01f);
    }
}

TEST_CASE ("The effects tap hears the reverb tail after the voices have stopped; the amp tap does not", "[effects][scope]")
{
    auto p = sineParams();
    p.reverb.enabled = true;
    p.reverb.mix = 0.5f;
    p.reverb.decaySeconds = 2.0f;

    Rig rig (p);
    std::vector<float> scope (8192);

    rig.noteOn (69);
    rig.runStereo (9600);
    rig.noteOff (69);
    rig.runStereo (4800); // the gate has closed, the reverb is ringing

    auto tapRms = [&] (engine::ScopeTap tap)
    {
        rig.engine.setScopeTap (tap);
        rig.runStereo (96); // the tap change is seen at the next sub-block
        rig.engine.readScope (scope.data(), (int) scope.size());
        const auto out = rig.runStereo (2400);
        const int got = rig.engine.readScope (scope.data(), (int) scope.size());
        REQUIRE (got == 2400);

        std::vector<float> mono ((size_t) got);
        double sum = 0.0, worstDifference = 0.0;
        for (size_t i = 0; i < (size_t) got; ++i)
        {
            mono[i] = 0.5f * (out.first[i] + out.second[i]);
            sum += (double) scope[i] * (double) scope[i];
            worstDifference = std::max (worstDifference, (double) std::abs (scope[i] - mono[i]));
        }

        struct Result { double scopeRms, outputRms, worstDifference; };
        return Result { std::sqrt (sum / got), rms (mono, 0, mono.size()), worstDifference };
    };

    const auto fx = tapRms (engine::ScopeTap::effects);
    REQUIRE (fx.scopeRms > 1.0e-3);
    REQUIRE (fx.worstDifference < 1.0e-6); // the tap is the (mono mix of the) output itself

    const auto amp = tapRms (engine::ScopeTap::amp);
    REQUIRE (amp.scopeRms == 0.0);
    REQUIRE (amp.outputRms > 1.0e-4); // while the output still carries the tail
}

TEST_CASE ("The engine feeds the summed voices through the effects", "[effects][engine]")
{
    // Delay at mix 1 on a short note: it is heard 100 ms later and not before. (The oscillator itself runs 16 samples late.)
    auto p = sineParams();
    p.osc[0].levelDb = -12.0f;
    p.delay = delayOnly (100.0f, 0.0f, false, 1.0f).delay;

    Rig rig (p);
    rig.noteOn (69);
    auto out = rig.runStereo (480);
    rig.noteOff (69);
    const auto rest = rig.runStereo (9600);
    out.first.insert (out.first.end(), rest.first.begin(), rest.first.end());

    REQUIRE (peakIn (out.first, 0, 4800) == 0.0f);
    REQUIRE (peakIn (out.first, 4800, 5400) > 0.1f);
    REQUIRE (peakIn (out.first, 6000, out.first.size()) < 1.0e-3f);
}
