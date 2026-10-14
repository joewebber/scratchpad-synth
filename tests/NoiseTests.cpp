#include "TestUtils.h"
#include "dsp/FloatCompare.h"
#include "dsp/Noise.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using scratchpad::dsp::Noise;

namespace
{
    constexpr size_t longRun = 1 << 20;

    std::vector<float> render (std::uint32_t seed, bool pink, size_t count)
    {
        Noise noise;
        noise.seed (seed);
        noise.setPink (pink);

        std::vector<float> out (count);
        for (auto& v : out)
            v = noise.next();

        return out;
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (const float v : x)
            sum += (double) v * (double) v;
        return std::sqrt (sum / (double) x.size());
    }

    double mean (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (const float v : x)
            sum += (double) v;
        return sum / (double) x.size();
    }

    /** Welch-averaged power density, then a least-squares line through its dB
        value per octave band (125 Hz to 16 kHz at 48 kHz, 4096-point frames).
        Returns dB per octave. */
    double spectralSlopeDbPerOctave (const std::vector<float>& x)
    {
        constexpr size_t frame = 4096;
        constexpr double sampleRate = 48000.0;

        std::vector<double> power (frame / 2 + 1, 0.0);
        for (size_t start = 0; start + frame <= x.size(); start += frame)
        {
            const std::vector<float> slice (x.begin() + (std::ptrdiff_t) start, x.begin() + (std::ptrdiff_t) (start + frame));
            const auto mag = testutil::hannSpectrum (slice);
            for (size_t i = 0; i < power.size(); ++i)
                power[i] += mag[i] * mag[i];
        }

        std::vector<double> octave, db;
        for (double low = 125.0; low < 12000.0; low *= 2.0)
        {
            double sum = 0.0;
            int bins = 0;
            for (size_t i = 0; i < power.size(); ++i)
            {
                const double hz = (double) i * sampleRate / (double) frame;
                if (hz >= low && hz < 2.0 * low)
                {
                    sum += power[i];
                    ++bins;
                }
            }

            octave.push_back (std::log2 (low * std::numbers::sqrt2));
            db.push_back (10.0 * std::log10 (sum / (double) bins));
        }

        const double n = (double) octave.size();
        double mx = 0.0, my = 0.0;
        for (size_t i = 0; i < octave.size(); ++i)
        {
            mx += octave[i];
            my += db[i];
        }
        mx /= n;
        my /= n;

        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < octave.size(); ++i)
        {
            num += (octave[i] - mx) * (db[i] - my);
            den += (octave[i] - mx) * (octave[i] - mx);
        }

        return num / den;
    }
}

TEST_CASE ("White noise is uniform in [-1, 1) with zero mean and RMS 1/sqrt(3)", "[noise]")
{
    const auto x = render (1, false, longRun);

    REQUIRE (*std::min_element (x.begin(), x.end()) >= -1.0f);
    REQUIRE (*std::max_element (x.begin(), x.end()) < 1.0f);
    REQUIRE (*std::min_element (x.begin(), x.end()) < -0.999f);
    REQUIRE (*std::max_element (x.begin(), x.end()) > 0.999f);

    REQUIRE (std::abs (mean (x)) < 0.003);
    REQUIRE (rms (x) == Catch::Approx (1.0 / std::sqrt (3.0)).margin (0.003));

    // Sixteen equal-width bins each hold about 1/16 of the samples.
    std::vector<int> histogram (16, 0);
    for (const float v : x)
        ++histogram[(size_t) std::clamp ((int) ((v + 1.0f) * 8.0f), 0, 15)];

    const double expected = (double) x.size() / 16.0;
    for (const int count : histogram)
        REQUIRE (std::abs ((double) count - expected) < 0.03 * expected);
}

TEST_CASE ("Pink noise has the same RMS as white noise", "[noise]")
{
    const auto white = render (1, false, longRun);
    const auto pink = render (1, true, longRun);

    const double ratio = rms (pink) / rms (white);
    INFO ("pink RMS " << rms (pink) << ", white RMS " << rms (white));
    REQUIRE (ratio > 0.9);
    REQUIRE (ratio < 1.1);

    REQUIRE (std::abs (mean (pink)) < 0.02);
}

TEST_CASE ("Pink noise falls 3 dB per octave while white noise is flat", "[noise]")
{
    const double pinkSlope = spectralSlopeDbPerOctave (render (7, true, longRun));
    const double whiteSlope = spectralSlopeDbPerOctave (render (7, false, longRun));

    INFO ("pink slope " << pinkSlope << " dB/oct, white slope " << whiteSlope << " dB/oct");
    REQUIRE (std::abs (pinkSlope + 3.0) < 1.0);
    REQUIRE (std::abs (whiteSlope) < 0.3);
}

TEST_CASE ("Noise is deterministic per seed and differs between seeds", "[noise]")
{
    for (const bool pink : { false, true })
    {
        const auto a = render (12345, pink, 4096);
        const auto b = render (12345, pink, 4096);
        const auto c = render (12346, pink, 4096);

        REQUIRE (a == b);
        REQUIRE (a != c);

        int differing = 0;
        for (size_t i = 0; i < a.size(); ++i)
            differing += scratchpad::dsp::same (a[i], c[i]) ? 0 : 1;
        REQUIRE (differing > 4000);
    }
}

TEST_CASE ("Reseeding restarts the sequence, including pink filter state", "[noise]")
{
    Noise noise;
    noise.setPink (true);
    noise.seed (99);

    std::vector<float> first (2048), second (2048);
    for (auto& v : first)
        v = noise.next();

    noise.seed (99);
    for (auto& v : second)
        v = noise.next();

    REQUIRE (first == second);
}

TEST_CASE ("Seed zero still produces noise", "[noise]")
{
    const auto x = render (0, false, 4096);

    REQUIRE (rms (x) > 0.5);
    REQUIRE (rms (x) < 0.65);
}

TEST_CASE ("Switching colour while running never produces NaN or runaway values", "[noise]")
{
    Noise noise;
    noise.seed (3);

    float peak = 0.0f;
    for (int i = 0; i < 200000; ++i)
    {
        noise.setPink ((i / 97) % 2 == 0);
        const float v = noise.next();

        REQUIRE (std::isfinite (v));
        peak = std::max (peak, std::abs (v));
    }

    REQUIRE (peak < 4.0f);
}
