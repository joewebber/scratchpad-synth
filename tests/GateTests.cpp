#include "dsp/Gate.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using scratchpad::dsp::Gate;

namespace
{
    constexpr double sampleRate = 48000.0;

    Gate makeGate()
    {
        Gate g;
        g.setSampleRate (sampleRate);
        return g;
    }
}

TEST_CASE ("Gate opens in 2 ms and closes in 2 ms", "[gate]")
{
    auto gate = makeGate();
    const int rampSamples = (int) (0.002 * sampleRate);

    gate.open();
    int up = 0;
    while (gate.getLevel() < 1.0f)
    {
        gate.next();
        ++up;
        REQUIRE (up <= rampSamples + 1);
    }
    REQUIRE (std::abs (up - rampSamples) <= 1);

    gate.close();
    int down = 0;
    while (! gate.isClosedAndSilent())
    {
        gate.next();
        ++down;
        REQUIRE (down <= rampSamples + 1);
    }
    REQUIRE (std::abs (down - rampSamples) <= 1);
}

TEST_CASE ("Gate never steps by more than one ramp increment, even when retargeted", "[gate]")
{
    auto gate = makeGate();
    const float maxStep = 1.0f / (float) (0.002 * sampleRate) * 1.001f;

    gate.open();
    float previous = 0.0f;

    // Open, close half way, re-open half way, close: every sample moves smoothly.
    for (int i = 0; i < 400; ++i)
    {
        if (i == 40)  gate.close();
        if (i == 70)  gate.open();
        if (i == 120) gate.close();

        const float level = gate.next();
        REQUIRE (std::abs (level - previous) <= maxStep);
        previous = level;
    }

    REQUIRE (gate.isClosedAndSilent());
}

TEST_CASE ("A steal fade takes 5 ms from full level", "[gate]")
{
    auto gate = makeGate();
    gate.open();
    for (int i = 0; i < 500; ++i)
        gate.next();
    REQUIRE (gate.getLevel() == 1.0f);

    gate.close (Gate::stealSeconds);
    int n = 0;
    while (! gate.isClosedAndSilent() && n < 1000)
    {
        gate.next();
        ++n;
    }

    REQUIRE (std::abs (n - (int) (0.005 * sampleRate)) <= 1);
}
