#include "dsp/FastFloor.h"
#include "dsp/FastTanh.h"
#include "dsp/FractionalRead.h"
#include "dsp/HalfbandDecimator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace scratchpad::dsp;

namespace
{
    std::uint64_t bitsOf (double x)
    {
        std::uint64_t b;
        std::memcpy (&b, &x, sizeof b);
        return b;
    }
}

TEST_CASE ("fastFloor is std::floor over the values a phase takes (negative zero aside)", "[dsp][fast]")
{
    std::mt19937_64 rng (7);
    std::uniform_real_distribution<double> small (-4.0, 4.0), large (-1.0e9, 1.0e9);

    std::vector<double> values { 0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, 1.0 - 1.0e-16, -1.0e-18, 1.0e-300, -1.0e-300,
                                 0.9999999999999999, 1.0000000000000002, -0.9999999999999999, 4503599627370496.0, -4503599627370497.0 };

    for (int i = 0; i < 200000; ++i)
    {
        values.push_back (small (rng));
        values.push_back (large (rng));
    }

    for (double v : values)
        REQUIRE (bitsOf (fastFloor (v) + 0.0) == bitsOf (std::floor (v) + 0.0)); // `+ 0.0` folds -0 into +0, the one thing that differs
}

TEST_CASE ("fastTanh stays within 5e-7 of tanh and keeps its shape", "[dsp][fast]")
{
    double worst = 0.0;

    for (double x = -12.0; x <= 12.0; x += 1.0e-4)
        worst = std::max (worst, std::abs ((double) fastTanh ((float) x) - std::tanh ((double) (float) x)));

    INFO ("worst error " << worst);
    REQUIRE (worst < 5.0e-7);

    // Small arguments keep their relative accuracy, which the ladder's linear region relies on.
    for (double x = 1.0e-7; x < 1.0e-2; x *= 1.3)
        REQUIRE (std::abs ((double) fastTanh ((float) x) / std::tanh (x) - 1.0) < 5.0e-6);

    REQUIRE (fastTanh (0.0f) == 0.0f);
    REQUIRE (fastTanh (1.0e30f) == fastTanh (8.0f));
    REQUIRE (std::abs (fastTanh (1.0e30f) - 1.0f) < 5.0e-7f);
    REQUIRE (std::abs (fastTanh (std::numeric_limits<float>::infinity()) - 1.0f) < 5.0e-7f);
    REQUIRE (std::abs (fastTanh (-std::numeric_limits<float>::infinity()) + 1.0f) < 5.0e-7f);
    REQUIRE (std::isnan (fastTanh (std::numeric_limits<float>::quiet_NaN())));

    float previous = -2.0f;
    for (float x = -9.0f; x <= 9.0f; x += 0.01f)
    {
        const float y = fastTanh (x);
        REQUIRE (y == -fastTanh (-x)); // odd
        REQUIRE (y >= previous - 1.0e-6f); // monotone, to within a few times the approximation's error
        REQUIRE (std::abs (y) <= 1.0f + 5.0e-7f);
        previous = y;
    }
}

TEST_CASE ("readCubic reads the same slots wherever the write index is, including across the wrap", "[dsp][fast]")
{
    constexpr int size = 64;
    std::vector<float> buffer (size);
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    for (auto& s : buffer)
        s = noise (rng);

    // The definition: the sample k steps older than `writeIndex` is buffer[(writeIndex - k) mod size].
    const auto reference = [&] (int writeIndex, float delay)
    {
        const int i = (int) delay;
        const float t = delay - (float) i;
        const auto at = [&] (int k) { return buffer[(size_t) (((writeIndex - k) % size + size) % size)]; };
        const float p0 = at (i - 1), p1 = at (i), p2 = at (i + 1), p3 = at (i + 2);
        return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
    };

    for (int writeIndex = 0; writeIndex < size; ++writeIndex)
        for (float delay : { 1.0f, 1.25f, 2.0f, 2.5f, 7.75f, 30.0f, 45.5f, 60.99f })
            REQUIRE (readCubic (buffer.data(), size, writeIndex, delay) == reference (writeIndex, delay));
}

TEST_CASE ("The half-band decimator has one impulse response however its history has wrapped", "[dsp][fast]")
{
    // An impulse in the first or the second of the two input samples of a step, followed by silence.
    const auto response = [] (int leadIn, bool inSecond)
    {
        HalfbandDecimator d;
        for (int i = 0; i < leadIn; ++i)
            d.process (0.0f, 0.0f);

        std::vector<float> out;
        out.push_back (d.process (inSecond ? 0.0f : 1.0f, inSecond ? 1.0f : 0.0f));
        for (int i = 1; i < 20; ++i)
            out.push_back (d.process (0.0f, 0.0f));
        return out;
    };

    const auto first = response (0, false);
    const auto second = response (0, true);

    // Where the impulse lands relative to the end of the history ring must not matter.
    for (int leadIn = 1; leadIn < 70; ++leadIn)
    {
        REQUIRE (response (leadIn, false) == first);
        REQUIRE (response (leadIn, true) == second);
    }

    // The filter is a half-band: an impulse on the even slots only meets the centre tap (half the DC gain, at the
    // filter's delay); one on the odd slots meets the other taps, and together they make unity gain at DC.
    float sumFirst = 0.0f, sumSecond = 0.0f;
    int nonzeroFirst = 0, nonzeroSecond = 0;

    for (size_t i = 0; i < first.size(); ++i)
    {
        sumFirst += first[i];
        sumSecond += second[i];
        nonzeroFirst += first[i] != 0.0f ? 1 : 0;
        nonzeroSecond += second[i] != 0.0f ? 1 : 0;
    }

    REQUIRE (nonzeroFirst == 1);
    REQUIRE (std::abs (first[HalfbandDecimator::delaySamples / 2]) > 0.49f);
    REQUIRE (nonzeroSecond >= 8);
    REQUIRE (sumFirst + sumSecond == Catch::Approx (1.0f).margin (0.01f));
}
