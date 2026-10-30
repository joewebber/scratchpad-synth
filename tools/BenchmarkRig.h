#pragma once

#include "engine/Modulation.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <ctime>
#endif

/** The performance scenario from the brief, shared by the benchmark tool and the `[.perf]` tests: eight held
    notes through `ScratchpadProcessor::processBlock` at 48 kHz in 256-sample blocks, with every module in the
    patch. The processor is driven, not the engine alone, so reading the parameters each block is part of the cost. */
namespace scratchpad::bench
{

namespace detail
{
    /** CPU seconds this thread has used so far. */
    inline double threadCpuSeconds()
    {
       #if JUCE_WINDOWS
        FILETIME creation, exit, kernel, user;
        GetThreadTimes (GetCurrentThread(), &creation, &exit, &kernel, &user);
        const auto toSeconds = [] (const FILETIME& t) { return (double) (((unsigned long long) t.dwHighDateTime << 32) | t.dwLowDateTime) * 1.0e-7; };
        return toSeconds (kernel) + toSeconds (user);
       #else
        timespec ts {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return (double) ts.tv_sec + (double) ts.tv_nsec * 1.0e-9;
       #endif
    }
}

inline constexpr double sampleRate = 48000.0;
inline constexpr int blockSize = 256;

struct Scenario
{
    int unison = 1;       // copies on every oscillator
    bool fm = true;       // oscillator 1 phase-modulates both others
    bool sync = true;     // hard sync on oscillators 2 and 3
    bool ring = true;     // ring modulation (osc 1 x osc 2)
};

struct Result
{
    double meanBlockMs = 0.0;
    double medianBlockMs = 0.0;
    double p99BlockMs = 0.0;
    double maxBlockMs = 0.0;
    double percentOfCore = 0.0; // total wall time in processBlock / total audio time
    double cpuPercent = 0.0;    // CPU time of this thread / audio time: steadier than wall time when the machine is busy
    float peak = 0.0f;
    bool finite = true;
};

inline void set (plugin::ScratchpadProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** One processor set up for the scenario, with the eight notes already held. */
class Rig
{
public:
    explicit Rig (const Scenario& s)
    {
        using namespace params;
        using engine::ModDest;
        using engine::ModSource;

        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        for (auto m : plugin::allModules)
            processor.addModule (m);

        set (processor, voicingVoices, 8.0f);

        const std::array<float, 3> waves { 2.0f, 3.0f, 1.0f }; // saw, pulse, triangle (see waveNames())
        for (size_t i = 0; i < 3; ++i)
        {
            set (processor, osc[i].wave, waves[i]);
            set (processor, osc[i].level, -12.0f);
            set (processor, osc[i].unison, (float) s.unison);
            set (processor, osc[i].unisonDetune, 25.0f);
            set (processor, osc[i].unisonSpread, 80.0f);
        }

        if (s.sync)
        {
            set (processor, osc[1].sync, 1.0f);
            set (processor, osc[2].sync, 1.0f);
        }

        set (processor, ringLevel, s.ring ? -12.0f : -60.0f);
        set (processor, fmAmount, s.fm ? 35.0f : 0.0f);
        set (processor, fmTarget, 2.0f); // both

        set (processor, noiseLevel, -24.0f);
        set (processor, filterMode, 0.0f); // LP 24
        set (processor, filterCutoff, 2500.0f);
        set (processor, filterResonance, 40.0f);
        set (processor, filterDrive, 6.0f);
        set (processor, filterKeyTrack, 50.0f);
        set (processor, filterEnvAmount, 40.0f);
        set (processor, lfo[0].retrigger, 1.0f); // a phase per voice
        set (processor, lfo[0].rate, 5.0f);
        set (processor, lfo[1].rate, 0.4f);

        const std::array<std::pair<ModSource, ModDest>, 8> connections {{
            { ModSource::lfo1, ModDest::pitchAll },
            { ModSource::lfo2, ModDest::cutoff },
            { ModSource::filterEnv, ModDest::pulseWidth2 },
            { ModSource::velocity, ModDest::level2 },
            { ModSource::key, ModDest::pan },
            { ModSource::ampEnv, ModDest::level3 },
            { ModSource::lfo1, ModDest::resonance },
            { ModSource::lfo2, ModDest::lfo1Rate }
        }};

        for (size_t i = 0; i < connections.size(); ++i)
        {
            set (processor, mod[i].source, (float) (int) connections[i].first);
            set (processor, mod[i].dest, (float) (int) connections[i].second);
            set (processor, mod[i].amount, 30.0f);
        }

        set (processor, distortionDrive, 18.0f);
        set (processor, delayPingPong, 1.0f);
        set (processor, outVolume, -12.0f);

        buffer.setSize (2, blockSize);
        process(); // let the parameter smoothers and the effect fades settle on the first block

        constexpr std::array<int, 8> notes { 43, 50, 55, 59, 62, 67, 71, 74 };
        for (size_t i = 0; i < notes.size(); ++i)
            midi.addEvent (juce::MidiMessage::noteOn (1, notes[i], 0.8f), (int) i);

        processor.processBlock (buffer, midi);
    }

    void process()
    {
        midi.clear();
        processor.processBlock (buffer, midi);
    }

    /** Runs `numBlocks` blocks, timing each. */
    Result run (int numBlocks)
    {
        using clock = std::chrono::steady_clock;

        std::vector<double> ms;
        ms.reserve ((size_t) numBlocks);
        Result r;
        double total = 0.0;

        const double cpuStart = detail::threadCpuSeconds();

        for (int i = 0; i < numBlocks; ++i)
        {
            midi.clear();
            const auto t0 = clock::now();
            processor.processBlock (buffer, midi);
            const auto t1 = clock::now();

            const double blockMs = std::chrono::duration<double, std::milli> (t1 - t0).count();
            ms.push_back (blockMs);
            total += blockMs;

            if (i % 50 == 0) // a sanity check on the audio, not part of what is timed
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < blockSize; ++n)
                    {
                        const float v = buffer.getReadPointer (ch)[n];
                        r.finite = r.finite && std::isfinite (v);
                        r.peak = std::max (r.peak, std::abs (v));
                    }
        }

        const double cpuSeconds = detail::threadCpuSeconds() - cpuStart;
        r.cpuPercent = 100.0 * cpuSeconds / ((double) numBlocks * blockSize / sampleRate);

        std::sort (ms.begin(), ms.end());
        r.meanBlockMs = total / numBlocks;
        r.medianBlockMs = ms[ms.size() / 2];
        r.p99BlockMs = ms[(size_t) ((double) ms.size() * 0.99)];
        r.maxBlockMs = ms.back();
        r.percentOfCore = 100.0 * r.meanBlockMs / (1000.0 * blockSize / sampleRate);
        return r;
    }

    plugin::ScratchpadProcessor processor;

    /** The block most recently processed. */
    const juce::AudioBuffer<float>& lastBlock() const noexcept { return buffer; }

private:
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
};

/** A hash of the first `numBlocks` blocks of audio (both channels, every bit): equal fingerprints mean an
    optimisation changed nothing about the sound. */
inline std::uint64_t fingerprint (const Scenario& s, int numBlocks = 200)
{
    Rig rig (s);
    std::uint64_t hash = 14695981039346656037ull;

    for (int b = 0; b < numBlocks; ++b)
    {
        rig.process();

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < blockSize; ++n)
            {
                std::uint32_t bits;
                const float v = rig.lastBlock().getReadPointer (ch)[n];
                std::memcpy (&bits, &v, sizeof bits);
                hash = (hash ^ bits) * 1099511628211ull;
            }
    }

    return hash;
}

} // namespace scratchpad::bench
