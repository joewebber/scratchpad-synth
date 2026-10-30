#include "../tools/BenchmarkRig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace scratchpad;

namespace
{
    std::vector<float> render (const bench::Scenario& s, int blocks)
    {
        bench::Rig rig (s);
        std::vector<float> out;

        for (int b = 0; b < blocks; ++b)
        {
            rig.process();
            const auto* l = rig.lastBlock().getReadPointer (0);
            out.insert (out.end(), l, l + bench::blockSize);
        }

        return out;
    }

    float rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (float v : x)
            sum += (double) v * v;
        return (float) std::sqrt (sum / (double) x.size());
    }

    /** Best (smallest) mean cost of several runs, in percent of one core: noise only adds time. */
    double bestPercent (const bench::Scenario& s, int runs = 3, int blocks = 600)
    {
        double best = 1.0e9;

        for (int r = 0; r < runs; ++r)
        {
            bench::Rig rig (s);
            rig.run (100);
            best = std::min (best, rig.run (blocks).percentOfCore);
        }

        return best;
    }
}

// The benchmark is only worth anything if it measures what it says, so the normal suite checks the scenario.

TEST_CASE ("The benchmark scenario is audible, finite and repeatable", "[perf-scenario]")
{
    const auto a = render ({ 7, true, true, true }, 80);
    const auto b = render ({ 7, true, true, true }, 80);

    REQUIRE (a == b); // the same patch and notes give the same samples, run to run

    REQUIRE (std::all_of (a.begin(), a.end(), [] (float v) { return std::isfinite (v) && std::abs (v) <= 1.0f; }));
    REQUIRE (rms (a) > 0.01f);
}

TEST_CASE ("The benchmark scenario has every module and the extras switched on", "[perf-scenario]")
{
    const auto plain = render ({ 1, false, false, false }, 80);

    // Each switch changes the audio, so each one is really on in the scenario the numbers describe.
    const auto differs = [&] (const bench::Scenario& s) { return render (s, 80) != plain; };

    CHECK (differs ({ 7, false, false, false }));
    CHECK (differs ({ 1, true,  false, false }));
    CHECK (differs ({ 1, false, true,  false }));
    CHECK (differs ({ 1, false, false, true  }));

    bench::Rig rig ({ 1, false, false, false });

    for (auto m : plugin::allModules)
        CHECK (rig.processor.isModulePresent (m));

    for (int slot = 0; slot < params::numModSlots; ++slot)
        CHECK (rig.processor.getModConnections()[(size_t) slot].active());
}

// Timing tests are hidden from the normal run (`ScratchpadTests "[perf]"` runs them, in a Release build on an
// idle machine). The thresholds are the brief's targets; ScratchpadBenchmark prints the full table.

TEST_CASE ("Eight voices with every module at unison 1 use under 15% of a core", "[.perf]")
{
    const double percent = bestPercent ({ 1, true, true, true });
    INFO ("measured " << percent << "% of one core");
    CHECK (percent < 15.0);
}

TEST_CASE ("Eight voices at unison 7 on all three oscillators use under 50% of a core", "[.perf]")
{
    const double percent = bestPercent ({ 7, true, true, true });
    INFO ("measured " << percent << "% of one core");
    CHECK (percent < 50.0);
}
