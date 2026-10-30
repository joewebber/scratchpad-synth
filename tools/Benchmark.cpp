// CPU benchmark for the performance target in the brief: eight held notes with every module in the patch,
// 48 kHz, 256-sample blocks, through ScratchpadProcessor::processBlock. Build in Release and run it on an idle
// machine. Each scenario is repeated and the minimum and median of the per-run means are printed: a noisy
// machine (a VM, WSL2, a laptop on battery) only ever adds time, so the minimum is the best estimate of the
// cost and the spread shows how much to trust it.
//
//   ScratchpadBenchmark [runs=7] [blocks-per-run=1500] [scenario=all (1-4: run only that row, for profiling)]

#include "BenchmarkRig.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace scratchpad;

namespace
{
    struct Row
    {
        const char* name;
        bench::Scenario scenario;
        double targetPercent;
    };
}

int main (int argc, char** argv)
{
    const int runs = argc > 1 ? std::max (1, std::atoi (argv[1])) : 7;
    const int blocks = argc > 2 ? std::max (100, std::atoi (argv[2])) : 1500;
    const int only = argc > 3 ? std::atoi (argv[3]) : 0;

    juce::ScopedJuceInitialiser_GUI initialiser;

    const std::vector<Row> rows {
        { "unison 1, FM + sync + ring on",  { 1, true,  true,  true  }, 15.0 },
        { "unison 1, FM + sync + ring off", { 1, false, false, false }, 15.0 },
        { "unison 7 x3, FM/sync/ring off",  { 7, false, false, false }, 50.0 },
        { "unison 7 x3, FM + sync + ring",  { 7, true,  true,  true  }, 50.0 }
    };

    std::printf ("%d runs x %d blocks of %d samples at %.0f Hz (%.1f s of audio per run), 8 voices\n\n",
                 runs, blocks, bench::blockSize, bench::sampleRate, blocks * bench::blockSize / bench::sampleRate);
    std::printf ("%-34s %7s %8s %7s %8s %9s %9s %9s  %s\n", "scenario", "wall min", "wall med", "cpu min", "cpu med", "med blk ms",
                 "p99 blk ms", "max blk ms", "target (wall median)");

    bool allMet = true;

    for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        if (only != 0 && (int) rowIndex + 1 != only)
            continue;

        const auto& row = rows[rowIndex];
        std::vector<double> percents, cpuPercents;
        bench::Result last;
        double medianBlock = 0.0, p99 = 0.0, maxBlock = 0.0;

        for (int r = 0; r < runs; ++r)
        {
            bench::Rig rig (row.scenario);
            rig.run (200); // warm-up: caches, branch predictors, frequency scaling
            last = rig.run (blocks);

            if (! last.finite || last.peak <= 0.0f)
            {
                std::printf ("%s: the scenario produced %s\n", row.name, last.finite ? "silence" : "a non-finite sample");
                return 2;
            }

            percents.push_back (last.percentOfCore);
            cpuPercents.push_back (last.cpuPercent);
            medianBlock += last.medianBlockMs / runs;
            p99 += last.p99BlockMs / runs;
            maxBlock = std::max (maxBlock, last.maxBlockMs);
        }

        std::sort (percents.begin(), percents.end());
        std::sort (cpuPercents.begin(), cpuPercents.end());
        const double best = percents.front(), median = percents[percents.size() / 2];
        const bool met = median < row.targetPercent;
        allMet = allMet && met;

        std::printf ("%-34s %7.2f %8.2f %7.2f %8.2f %9.3f %9.3f %9.3f  < %.0f%% %s\n", row.name, best, median, cpuPercents.front(),
                     cpuPercents[cpuPercents.size() / 2], medianBlock, p99, maxBlock, row.targetPercent, met ? "met" : "MISSED");
    }

    std::printf ("\noutput fingerprints (FNV-1a of the first 200 blocks; equal means the sound did not change):\n");
    for (const auto& row : rows)
        std::printf ("  %-34s %016llx\n", row.name, (unsigned long long) bench::fingerprint (row.scenario));

    return allMet ? 0 : 1;
}
