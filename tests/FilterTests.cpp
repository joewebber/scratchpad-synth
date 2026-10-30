#include "dsp/FloatCompare.h"
#include "dsp/LadderFilter.h"
#include "dsp/MultimodeFilter.h"
#include "dsp/Noise.h"
#include "dsp/SvfFilter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace scratchpad::dsp;

namespace
{
    constexpr double sampleRates[] = { 48000.0, 44100.0 };

    double toDb (double ratio) { return 20.0 * std::log10 (ratio); }

    /** Steady-state gain of `process` (a float -> float callable that owns its
        filter) for a sine of the given frequency. Settles first, then takes a
        Hann-weighted single-bin DFT so a non-integer number of cycles does
        not bias the result. */
    template <typename Process>
    double gainAt (Process&& process, double freq, double sampleRate, double amplitude,
                   double settleSeconds = 0.5, double measureSeconds = 0.25)
    {
        const int settle = (int) (settleSeconds * sampleRate);
        const int measure = (int) (measureSeconds * sampleRate);
        const double w = 2.0 * std::numbers::pi * freq / sampleRate;

        double re = 0.0, im = 0.0, weightSum = 0.0;
        for (int n = 0; n < settle + measure; ++n)
        {
            const float y = process ((float) (amplitude * std::sin (w * (double) n)));
            if (n < settle)
                continue;

            const double pos = (double) (n - settle) / (double) measure;
            const double weight = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * pos);
            re += weight * (double) y * std::cos (w * (double) n);
            im += weight * (double) y * std::sin (w * (double) n);
            weightSum += weight;
        }

        return 2.0 * std::hypot (re, im) / weightSum / amplitude;
    }

    /** 1 / damping, the Q the SVF header documents. */
    double svfDamping (double resonance)
    {
        return 1.41421356 * (1.0 - resonance) - 0.02 * resonance * resonance * resonance * resonance;
    }

    /** Gain of the four-pole ladder at its cutoff: each pole is 1 / (1 + j) there,
        the four together are -1/4, and the loop feedback is k = 4.1 * resonance. */
    double ladderGainAtCutoff (double resonance)
    {
        return 0.25 / std::abs (1.0 - 4.1 * resonance / 4.0);
    }

    struct SvfRig
    {
        SvfRig (double sr, float cutoff, float resonance)
        {
            filter.setSampleRate (sr);
            filter.setParams (cutoff, resonance);
        }

        SvfFilter filter;
    };

    struct LadderRig
    {
        LadderRig (double sr, float cutoff, float resonance)
        {
            filter.setSampleRate (sr);
            filter.setParams (cutoff, resonance);
        }

        LadderFilter filter;
    };

    struct RingResult
    {
        bool finite = true;
        float peak = 0.0f;
        double earlyRms = 0.0;   // 1.00 s to 1.25 s
        double lateRms = 0.0;    // 1.75 s to 2.00 s
        double tailPeak = 0.0;   // last 0.5 s
        double frequency = 0.0;  // from upward zero crossings in the last 0.5 s
    };

    /** Hits the filter with a single impulse, then lets it run for two seconds. */
    template <typename Process>
    RingResult ringAfterImpulse (Process&& process, double sampleRate, float impulse)
    {
        RingResult result;
        const int total = (int) (2.0 * sampleRate);
        std::vector<float> y ((size_t) total);

        for (int i = 0; i < total; ++i)
        {
            y[(size_t) i] = process (i == 0 ? impulse : 0.0f);
            result.finite = result.finite && std::isfinite (y[(size_t) i]);
            result.peak = std::max (result.peak, std::abs (y[(size_t) i]));
        }

        const auto rms = [&] (double from, double to)
        {
            double sum = 0.0;
            const int a = (int) (from * sampleRate), b = (int) (to * sampleRate);
            for (int i = a; i < b; ++i)
                sum += (double) y[(size_t) i] * (double) y[(size_t) i];
            return std::sqrt (sum / (double) (b - a));
        };

        result.earlyRms = rms (1.0, 1.25);
        result.lateRms = rms (1.75, 2.0);

        std::vector<double> crossings;
        for (int i = (int) (1.5 * sampleRate); i + 1 < total; ++i)
        {
            const double a = (double) y[(size_t) i], b = (double) y[(size_t) i + 1];
            if (a <= 0.0 && b > 0.0)
                crossings.push_back ((double) i - a / (b - a));
            result.tailPeak = std::max (result.tailPeak, std::abs (a));
        }

        if (crossings.size() > 2)
            result.frequency = (double) (crossings.size() - 1) * sampleRate / (crossings.back() - crossings.front());

        return result;
    }

    /** Runs a filter over fixed-seed white noise and reports whether it stayed sane. */
    template <typename Process>
    bool staysBounded (Process&& process, int samples, float bound)
    {
        Noise noise;
        noise.seed (5);

        for (int i = 0; i < samples; ++i)
        {
            const float y = process (0.25f * noise.next());
            if (! std::isfinite (y) || std::abs (y) > bound)
                return false;
        }

        return true;
    }
}

// ---------------------------------------------------------------- SVF

TEST_CASE ("SVF low, high and band-pass magnitude at the cutoff equals the documented Q", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 1000.0f, 5000.0f })
            for (const float resonance : { 0.0f, 0.3f, 0.6f, 0.8f, 0.9f })
            {
                const double expected = 1.0 / svfDamping ((double) resonance);

                for (int output = 0; output < 3; ++output)
                {
                    SvfRig rig (sr, cutoff, resonance);
                    const double gain = gainAt ([&] (float x)
                                                {
                                                    const auto o = rig.filter.process (x);
                                                    return output == 0 ? o.low : output == 1 ? o.high : o.band;
                                                },
                                                (double) cutoff, sr, 0.001);

                    INFO ("sr " << sr << " cutoff " << cutoff << " resonance " << resonance << " output " << output);
                    REQUIRE (std::abs (toDb (gain / expected)) < 1.0);
                }
            }
}

TEST_CASE ("SVF has unity passband gain at resonance 0", "[filter]")
{
    for (const double sr : sampleRates)
    {
        SvfRig lowRig (sr, 1000.0f, 0.0f);
        const double low = gainAt ([&] (float x) { return lowRig.filter.process (x).low; }, 50.0, sr, 0.1);

        SvfRig highRig (sr, 500.0f, 0.0f);
        const double high = gainAt ([&] (float x) { return highRig.filter.process (x).high; }, 8000.0, sr, 0.1);

        INFO ("sr " << sr);
        REQUIRE (std::abs (toDb (low)) < 0.1);
        REQUIRE (std::abs (toDb (high)) < 0.1);
    }
}

TEST_CASE ("SVF low-pass falls 12 dB/octave and high-pass rises 12 dB/octave", "[filter]")
{
    for (const double sr : sampleRates)
    {
        SvfRig lowRig (sr, 200.0f, 0.0f);
        const double lowNear = gainAt ([&] (float x) { return lowRig.filter.process (x).low; }, 1600.0, sr, 0.1);
        lowRig.filter.reset();
        const double lowFar = gainAt ([&] (float x) { return lowRig.filter.process (x).low; }, 3200.0, sr, 0.1);

        SvfRig highRig (sr, 2000.0f, 0.0f);
        const double highLow = gainAt ([&] (float x) { return highRig.filter.process (x).high; }, 250.0, sr, 0.1);
        highRig.filter.reset();
        const double highHigh = gainAt ([&] (float x) { return highRig.filter.process (x).high; }, 500.0, sr, 0.1);

        INFO ("sr " << sr);
        REQUIRE (std::abs (toDb (lowNear / lowFar) - 12.0) < 1.0);
        REQUIRE (std::abs (toDb (highHigh / highLow) - 12.0) < 1.0);
    }
}

TEST_CASE ("SVF at full resonance rings steadily at the cutoff", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 440.0f, 2000.0f })
        {
            SvfRig rig (sr, cutoff, 1.0f);
            const auto ring = ringAfterImpulse ([&] (float x) { return rig.filter.process (x).low; }, sr, 0.1f);

            INFO ("sr " << sr << " cutoff " << cutoff << " peak " << ring.peak << " rms " << ring.earlyRms << " -> " << ring.lateRms
                        << " frequency " << ring.frequency);
            REQUIRE (ring.finite);
            REQUIRE (ring.peak < 3.0f);
            // "About one" per the header: loud enough to be a usable sine, whatever the cutoff.
            REQUIRE (ring.peak > 0.2f);
            REQUIRE (ring.lateRms > 0.9 * ring.earlyRms);
            REQUIRE (ring.lateRms < 1.1 * ring.earlyRms);
            REQUIRE (std::abs (ring.frequency / (double) cutoff - 1.0) < 0.01);
        }
}

TEST_CASE ("SVF just below full resonance decays", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 2000.0f })
            for (const float resonance : { 0.5f, 0.89f })
            {
                SvfRig rig (sr, cutoff, resonance);
                const auto ring = ringAfterImpulse ([&] (float x) { return rig.filter.process (x).low; }, sr, 0.1f);

                INFO ("sr " << sr << " cutoff " << cutoff << " resonance " << resonance);
                REQUIRE (ring.finite);
                REQUIRE (ring.tailPeak < 1.0e-4);
            }
}

TEST_CASE ("SVF stays stable with the cutoff above 0.45 x the sample rate", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float resonance : { 0.0f, 1.0f })
            for (const double multiple : { 0.45, 0.5, 1.0, 20.0 })
            {
                SvfRig rig (sr, (float) (multiple * sr), resonance);
                INFO ("sr " << sr << " multiple " << multiple << " resonance " << resonance);
                REQUIRE (staysBounded ([&] (float x) { return rig.filter.process (x).low; }, (int) sr, 10.0f));
            }
}

// ---------------------------------------------------------------- Ladder

TEST_CASE ("Ladder magnitude at the cutoff follows the four-pole feedback loop", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 1000.0f, 5000.0f })
            for (const float resonance : { 0.0f, 0.4f, 0.7f, 0.85f })
            {
                LadderRig rig (sr, cutoff, resonance);
                const double gain = gainAt ([&] (float x) { return rig.filter.process (x); }, (double) cutoff, sr, 0.001);

                INFO ("sr " << sr << " cutoff " << cutoff << " resonance " << resonance);
                REQUIRE (std::abs (toDb (gain / ladderGainAtCutoff ((double) resonance))) < 1.0);
            }
}

TEST_CASE ("Ladder falls 24 dB/octave above the cutoff", "[filter]")
{
    for (const double sr : sampleRates)
    {
        LadderRig rig (sr, 200.0f, 0.0f);
        const double near = gainAt ([&] (float x) { return rig.filter.process (x); }, 1600.0, sr, 0.1);
        rig.filter.reset();
        const double far = gainAt ([&] (float x) { return rig.filter.process (x); }, 3200.0, sr, 0.1);

        INFO ("sr " << sr);
        REQUIRE (std::abs (toDb (near / far) - 24.0) < 1.5);
    }
}

TEST_CASE ("Ladder passband has unity gain at resonance 0 and does not distort at normal levels", "[filter]")
{
    for (const double sr : sampleRates)
        for (const double amplitude : { 0.01, 0.5 })
        {
            LadderRig rig (sr, 2000.0f, 0.0f);
            const double gain = gainAt ([&] (float x) { return rig.filter.process (x); }, 40.0, sr, amplitude);

            INFO ("sr " << sr << " amplitude " << amplitude);
            REQUIRE (std::abs (toDb (gain)) < 0.1);
        }
}

TEST_CASE ("Ladder at full resonance rings steadily at the cutoff", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 440.0f, 2000.0f })
        {
            LadderRig rig (sr, cutoff, 1.0f);
            const auto ring = ringAfterImpulse ([&] (float x) { return rig.filter.process (x); }, sr, 0.1f);

            INFO ("sr " << sr << " cutoff " << cutoff << " peak " << ring.peak << " rms " << ring.earlyRms << " -> " << ring.lateRms
                        << " frequency " << ring.frequency);
            REQUIRE (ring.finite);
            REQUIRE (ring.peak < 3.0f);
            REQUIRE (ring.peak > 0.2f);
            REQUIRE (ring.lateRms > 0.9 * ring.earlyRms);
            REQUIRE (ring.lateRms < 1.1 * ring.earlyRms);
            REQUIRE (std::abs (ring.frequency / (double) cutoff - 1.0) < 0.01);
        }
}

TEST_CASE ("Ladder just below full resonance decays", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float cutoff : { 200.0f, 2000.0f })
            for (const float resonance : { 0.5f, 0.89f })
            {
                LadderRig rig (sr, cutoff, resonance);
                const auto ring = ringAfterImpulse ([&] (float x) { return rig.filter.process (x); }, sr, 0.1f);

                INFO ("sr " << sr << " cutoff " << cutoff << " resonance " << resonance);
                REQUIRE (ring.finite);
                REQUIRE (ring.tailPeak < 1.0e-4);
            }
}

TEST_CASE ("Ladder stays stable with the cutoff above 0.45 x the sample rate", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float resonance : { 0.0f, 1.0f })
            for (const double multiple : { 0.45, 0.5, 1.0, 20.0 })
            {
                LadderRig rig (sr, (float) (multiple * sr), resonance);
                INFO ("sr " << sr << " multiple " << multiple << " resonance " << resonance);
                REQUIRE (staysBounded ([&] (float x) { return rig.filter.process (x); }, (int) sr, 10.0f));
            }
}

// ---------------------------------------------------------------- Multimode

namespace
{
    constexpr FilterMode allModes[] = { FilterMode::lp24, FilterMode::lp12, FilterMode::hp12, FilterMode::bp12 };

    MultimodeFilter makeMultimode (double sr, FilterMode mode, float cutoff, float resonance, float driveDb = 0.0f)
    {
        MultimodeFilter filter;
        filter.prepare (sr);
        filter.setMode (mode);
        filter.setParams (cutoff, resonance, driveDb);
        return filter;
    }
}

TEST_CASE ("Multimode filter magnitude at the cutoff matches each mode's documented gain", "[filter]")
{
    for (const double sr : sampleRates)
        for (const float resonance : { 0.0f, 0.5f })
            for (const FilterMode mode : allModes)
            {
                auto filter = makeMultimode (sr, mode, 1000.0f, resonance);
                const double gain = gainAt ([&] (float x) { return filter.process (x); }, 1000.0, sr, 0.001);
                const double expected = mode == FilterMode::lp24 ? ladderGainAtCutoff ((double) resonance)
                                                                 : 1.0 / svfDamping ((double) resonance);

                INFO ("sr " << sr << " mode " << (int) mode << " resonance " << resonance);
                REQUIRE (std::abs (toDb (gain / expected)) < 1.0);
            }
}

TEST_CASE ("Multimode filter slopes: LP12 -12, LP24 -24, HP12 +12 dB/octave", "[filter]")
{
    for (const double sr : sampleRates)
    {
        const auto slope = [&] (FilterMode mode, float cutoff, double lowFreq, double highFreq)
        {
            auto filter = makeMultimode (sr, mode, cutoff, 0.0f);
            const double a = gainAt ([&] (float x) { return filter.process (x); }, lowFreq, sr, 0.1);
            filter.reset();
            const double b = gainAt ([&] (float x) { return filter.process (x); }, highFreq, sr, 0.1);
            return toDb (b / a);
        };

        INFO ("sr " << sr);
        REQUIRE (std::abs (slope (FilterMode::lp12, 200.0f, 1600.0, 3200.0) + 12.0) < 1.0);
        REQUIRE (std::abs (slope (FilterMode::lp24, 200.0f, 1600.0, 3200.0) + 24.0) < 1.5);
        REQUIRE (std::abs (slope (FilterMode::hp12, 2000.0f, 250.0, 500.0) - 12.0) < 1.0);
    }
}

TEST_CASE ("Multimode filter at full resonance rings at the cutoff in every mode", "[filter]")
{
    for (const double sr : sampleRates)
        for (const FilterMode mode : allModes)
        {
            auto filter = makeMultimode (sr, mode, 1000.0f, 1.0f);
            const auto ring = ringAfterImpulse ([&] (float x) { return filter.process (x); }, sr, 0.1f);

            INFO ("sr " << sr << " mode " << (int) mode << " peak " << ring.peak << " frequency " << ring.frequency);
            REQUIRE (ring.finite);
            REQUIRE (ring.peak < 3.0f);
            REQUIRE (ring.lateRms > 0.9 * ring.earlyRms);
            REQUIRE (ring.lateRms < 1.1 * ring.earlyRms);
            REQUIRE (std::abs (ring.frequency / 1000.0 - 1.0) < 0.01);
        }
}

TEST_CASE ("Multimode filter clamps the cutoff to 20 Hz .. 0.45 x the sample rate", "[filter]")
{
    for (const double sr : sampleRates)
        for (const FilterMode mode : allModes)
            for (const float resonance : { 0.0f, 1.0f })
                for (const float cutoff : { -100.0f, 0.0f, 1.0f, 19.0f, (float) (0.5 * sr), (float) sr, 1.0e6f })
                {
                    auto filter = makeMultimode (sr, mode, cutoff, resonance);
                    INFO ("sr " << sr << " mode " << (int) mode << " resonance " << resonance << " cutoff " << cutoff);
                    REQUIRE (staysBounded ([&] (float x) { return filter.process (x); }, (int) sr, 10.0f));
                }

    // Out-of-range cutoffs behave exactly like the nearest limit.
    for (const double sr : sampleRates)
    {
        auto low = makeMultimode (sr, FilterMode::lp12, 1.0f, 0.5f);
        auto lowLimit = makeMultimode (sr, FilterMode::lp12, 20.0f, 0.5f);
        auto high = makeMultimode (sr, FilterMode::lp12, 1.0e6f, 0.5f);
        auto highLimit = makeMultimode (sr, FilterMode::lp12, (float) (0.45 * sr), 0.5f);

        Noise noise;
        noise.seed (11);
        for (int i = 0; i < 2000; ++i)
        {
            const float x = 0.25f * noise.next();
            REQUIRE (same (low.process (x), lowLimit.process (x)));
            REQUIRE (same (high.process (x), highLimit.process (x)));
        }
    }
}

TEST_CASE ("Multimode filter survives a cutoff change on every sample without clicking", "[filter]")
{
    // A 220 Hz sine at 0.5 moves at most 0.5 * 2 pi * 220 / sr = 0.0144 per
    // sample. At resonance 0.5 no mode amplifies a sine by more than Q = 1.42,
    // so a clean output moves by at most ~0.021. The bound leaves 2.5x for the
    // filter's own response to the moving cutoff; a zipper click or an
    // instability steps by far more.
    constexpr float maxStep = 0.05f;

    for (const double sr : sampleRates)
        for (const FilterMode mode : allModes)
        {
            auto filter = makeMultimode (sr, mode, 50.0f, 0.5f);
            const int sweepLength = (int) (0.2 * sr);
            const double w = 2.0 * std::numbers::pi * 220.0 / sr;

            float previous = 0.0f;
            float biggestStep = 0.0f, peak = 0.0f;
            bool finite = true;

            for (int n = 0; n < 2 * sweepLength; ++n)
            {
                // 50 Hz to 12 kHz and back, exponentially.
                const double position = n < sweepLength ? (double) n / sweepLength : 2.0 - (double) n / sweepLength;
                filter.setParams ((float) (50.0 * std::pow (240.0, position)), 0.5f, 0.0f);

                const float y = filter.process ((float) (0.5 * std::sin (w * (double) n)));
                finite = finite && std::isfinite (y);

                if (n > 0)
                    biggestStep = std::max (biggestStep, std::abs (y - previous));
                peak = std::max (peak, std::abs (y));
                previous = y;
            }

            INFO ("sr " << sr << " mode " << (int) mode << " biggest step " << biggestStep << " peak " << peak);
            REQUIRE (finite);
            REQUIRE (biggestStep <= maxStep);
            REQUIRE (peak < 1.5f);
        }
}

TEST_CASE ("Multimode filter stays stable when the cutoff jumps every sample", "[filter]")
{
    for (const double sr : sampleRates)
        for (const FilterMode mode : allModes)
            for (const float resonance : { 0.9f, 1.0f })
            {
                auto filter = makeMultimode (sr, mode, 100.0f, resonance);
                Noise noise;
                noise.seed (21);

                bool ok = true;
                for (int n = 0; n < (int) sr && ok; ++n)
                {
                    filter.setParams (n % 2 == 0 ? 100.0f : 10000.0f, resonance, 0.0f);
                    const float y = filter.process (0.25f * noise.next());
                    ok = std::isfinite (y) && std::abs (y) < 10.0f;
                }

                INFO ("sr " << sr << " mode " << (int) mode << " resonance " << resonance);
                REQUIRE (ok);
            }
}

TEST_CASE ("Changing mode crossfades over 10 ms without a step", "[filter]")
{
    struct Switch { FilterMode from, to; };
    const Switch switches[] = {
        { FilterMode::lp24, FilterMode::hp12 }, { FilterMode::hp12, FilterMode::lp12 },
        { FilterMode::lp12, FilterMode::lp24 }, { FilterMode::bp12, FilterMode::hp12 },
        { FilterMode::lp24, FilterMode::bp12 }, { FilterMode::hp12, FilterMode::lp24 },
    };

    for (const double sr : sampleRates)
        for (const auto& [from, to] : switches)
        {
            constexpr double frequency = 700.0;
            const double w = 2.0 * std::numbers::pi * frequency / sr;
            const int switchAt = (int) (0.2 * sr);
            const int fadeLength = (int) (MultimodeFilter::switchSeconds * sr);
            const int total = switchAt + (int) (0.3 * sr);
            const int cycleSamples = (int) (2.0 * sr / frequency);

            auto filter = makeMultimode (sr, from, 1000.0f, 0.0f);
            auto fromReference = makeMultimode (sr, from, 1000.0f, 0.0f);
            auto toReference = makeMultimode (sr, to, 1000.0f, 0.0f);

            std::vector<float> out ((size_t) total), fromOut ((size_t) total), toOut ((size_t) total);
            for (int n = 0; n < total; ++n)
            {
                if (n == switchAt)
                    filter.setMode (to);

                const float x = (float) (0.5 * std::sin (w * (double) n));
                out[(size_t) n] = filter.process (x);
                fromOut[(size_t) n] = fromReference.process (x);
                toOut[(size_t) n] = toReference.process (x);
            }

            // Steady-state size and speed of the two modes just before the switch.
            double fromStep = 0.0, toStep = 0.0, fromPeak = 0.0, toPeak = 0.0;
            for (int n = switchAt - cycleSamples; n < switchAt; ++n)
            {
                fromStep = std::max (fromStep, (double) std::abs (fromOut[(size_t) n] - fromOut[(size_t) n - 1]));
                toStep = std::max (toStep, (double) std::abs (toOut[(size_t) n] - toOut[(size_t) n - 1]));
                fromPeak = std::max (fromPeak, (double) std::abs (fromOut[(size_t) n]));
                toPeak = std::max (toPeak, (double) std::abs (toOut[(size_t) n]));
            }

            // During the fade out = before + (now - before) * t with t rising by
            // 1 / fadeLength per sample, so each step is a blend of the two modes'
            // own steps plus (now - before) / fadeLength. 25% allows for the
            // incoming filter starting from rest (or stale state).
            const double bound = 1.25 * std::max (fromStep, toStep) + (fromPeak + toPeak) / (double) fadeLength;

            double biggestStep = 0.0;
            for (int n = switchAt; n < switchAt + fadeLength + 2; ++n)
                biggestStep = std::max (biggestStep, (double) std::abs (out[(size_t) n] - out[(size_t) n - 1]));

            INFO ("sr " << sr << " from " << (int) from << " to " << (int) to << " biggest step " << biggestStep << " bound " << bound);
            REQUIRE (biggestStep <= bound);

            // Once the fade is over (and the incoming filter has settled) the
            // output is the new mode's steady state.
            for (int n = total - (int) (0.05 * sr); n < total; ++n)
                REQUIRE (std::abs (out[(size_t) n] - toOut[(size_t) n]) < 1.0e-4f);

            // And the crossfade really is a crossfade: halfway through, the output
            // is between the old and new outputs rather than already one of them.
            // (Skipped when the two coincide at that instant.)
            const size_t mid = (size_t) (switchAt + fadeLength / 2);
            const float lo = std::min (fromOut[mid], toOut[mid]), hi = std::max (fromOut[mid], toOut[mid]);
            if (hi - lo > 0.05f)
            {
                // The incoming filter is still settling, so allow slack around the range.
                REQUIRE (out[mid] > lo - 0.1f);
                REQUIRE (out[mid] < hi + 0.1f);
            }
        }
}

TEST_CASE ("Rapid mode switching stays finite and bounded", "[filter]")
{
    for (const double sr : sampleRates)
    {
        auto filter = makeMultimode (sr, FilterMode::lp24, 800.0f, 0.7f);
        Noise noise;
        noise.seed (31);

        const int interval = (int) (0.003 * sr);
        bool ok = true;
        for (int n = 0; n < (int) sr && ok; ++n)
        {
            if (n % interval == 0)
                filter.setMode (allModes[(n / interval) % 4]);

            const float y = filter.process (0.25f * noise.next());
            ok = std::isfinite (y) && std::abs (y) < 5.0f;
        }

        INFO ("sr " << sr);
        REQUIRE (ok);
    }
}

TEST_CASE ("Drive at 0 dB is transparent", "[filter]")
{
    for (const double sr : sampleRates)
        for (const FilterMode mode : allModes)
        {
            auto filter = makeMultimode (sr, mode, 1500.0f, 0.6f, 0.0f);

            // The filter the mode wraps, driven directly with the same parameters.
            LadderFilter ladder;
            ladder.setSampleRate (sr);
            ladder.setParams (1500.0f, 0.6f);
            SvfFilter svf;
            svf.setSampleRate (sr);
            svf.setParams (1500.0f, 0.6f);

            Noise noise;
            noise.seed (41);

            // Setting a mode other than the default starts a 10 ms crossfade; compare once it is over.
            const int fadeLength = (int) (MultimodeFilter::switchSeconds * sr);

            float worst = 0.0f;
            for (int n = 0; n < 20000; ++n)
            {
                const float x = noise.next();
                const auto s = svf.process (x);
                const float l = ladder.process (x);
                const float expected = mode == FilterMode::lp24 ? l
                                     : mode == FilterMode::lp12 ? s.low
                                     : mode == FilterMode::hp12 ? s.high
                                                                : s.band;
                const float y = filter.process (x);
                if (n > fadeLength)
                    worst = std::max (worst, std::abs (y - expected));
            }

            INFO ("sr " << sr << " mode " << (int) mode);
            REQUIRE (worst < 1.0e-6f);
        }
}

TEST_CASE ("Drive compresses loud input and keeps the small-signal gain finite", "[filter]")
{
    for (const double sr : sampleRates)
        for (const FilterMode mode : { FilterMode::lp24, FilterMode::lp12 })
        {
            const auto passbandPeak = [&] (float driveDb, double amplitude)
            {
                auto filter = makeMultimode (sr, mode, 5000.0f, 0.0f, driveDb);
                const double w = 2.0 * std::numbers::pi * 100.0 / sr;
                float peak = 0.0f;
                for (int n = 0; n < (int) (0.4 * sr); ++n)
                {
                    const float y = filter.process ((float) (amplitude * std::sin (w * (double) n)));
                    REQUIRE (std::isfinite (y));
                    if (n > (int) (0.1 * sr))
                        peak = std::max (peak, std::abs (y));
                }
                return (double) peak;
            };

            const auto smallSignalGain = [&] (float driveDb)
            {
                auto filter = makeMultimode (sr, mode, 5000.0f, 0.0f, driveDb);
                return gainAt ([&] (float x) { return filter.process (x); }, 100.0, sr, 0.001, 0.2, 0.1);
            };

            INFO ("sr " << sr << " mode " << (int) mode);

            // Loud input: the output peak is held near the tanh ceiling (1 / sqrt (drive)
            // after make-up; the low-pass may overshoot a near-square wave a little).
            for (const float driveDb : { 12.0f, 24.0f })
            {
                const double ceiling = 1.0 / std::sqrt (std::pow (10.0, (double) driveDb / 20.0));
                INFO ("drive " << driveDb);
                REQUIRE (passbandPeak (driveDb, 10.0) < 4.0 * ceiling); // per-stage saturation uses less makeup than the legacy filter
                REQUIRE (passbandPeak (driveDb, 10.0) < 1.2 * passbandPeak (driveDb, 1.0));
            }

            // Small signals: finite, positive, never attenuated, and bounded even at 24 dB.
            double previous = smallSignalGain (0.0f);
            REQUIRE (std::abs (toDb (previous)) < 0.1);
            for (const float driveDb : { 12.0f, 24.0f })
            {
                const double gain = smallSignalGain (driveDb);
                INFO ("drive " << driveDb << " gain " << gain);
                REQUIRE (std::isfinite (gain));
                REQUIRE (gain >= previous);
                REQUIRE (toDb (gain) < 24.0);
                previous = gain;
            }
        }
}
