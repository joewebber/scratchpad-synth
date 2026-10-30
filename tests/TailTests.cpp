#include "plugin/PluginProcessor.h"

#include "../tools/BenchmarkRig.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace scratchpad;
using plugin::Module;
using plugin::ScratchpadProcessor;

namespace
{
    double tailOf (ScratchpadProcessor& p) { return p.getTailLengthSeconds(); }

    /** How long the sound really goes on after the last note-off, in seconds, found by playing it. */
    double measuredTail (ScratchpadProcessor& p)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int block = 512;
        p.setRateAndBufferSizeDetails (sampleRate, block);
        p.prepareToPlay (sampleRate, block);

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

        for (int i = 0; i < 40; ++i)
        {
            p.processBlock (buffer, midi);
            midi.clear();
        }

        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

        int lastAudible = 0;
        const int total = (int) (40.0 * sampleRate / block);

        for (int i = 0; i < total; ++i)
        {
            p.processBlock (buffer, midi);
            midi.clear();

            if (buffer.getMagnitude (0, block) > 0.001f) // -60 dB from a full-scale note
                lastAudible = i + 1;
        }

        return (double) lastAudible * block / sampleRate;
    }
}

TEST_CASE ("A bare patch has a short tail", "[tail]")
{
    ScratchpadProcessor p;
    REQUIRE (tailOf (p) > 0.0);
    REQUIRE (tailOf (p) < 0.1);
}

TEST_CASE ("The tail follows the amp envelope's release", "[tail]")
{
    ScratchpadProcessor p;
    p.addModule (Module::ampEnv);
    bench::set (p, params::ampEnv.release, 5.0f);
    REQUIRE (tailOf (p) >= 5.0);
    REQUIRE (tailOf (p) < 5.1);

    bench::set (p, params::ampEnv.release, 0.2f);
    REQUIRE (tailOf (p) < 0.4);

    // The envelope's release no longer counts when the envelope is bypassed (the gate takes over).
    bench::set (p, params::ampEnv.release, 5.0f);
    bench::set (p, "ampEnv.bypass", 1.0f);
    REQUIRE (tailOf (p) < 0.1);
}

TEST_CASE ("The tail includes the reverb's decay time while the reverb is present and not bypassed", "[tail]")
{
    ScratchpadProcessor p;
    const double bare = tailOf (p);

    p.addModule (Module::reverb);
    bench::set (p, params::reverbDecay, 8.0f);
    REQUIRE (tailOf (p) >= bare + 8.0);
    REQUIRE (tailOf (p) < bare + 8.5);

    bench::set (p, "reverb.bypass", 1.0f);
    REQUIRE (tailOf (p) == bare);

    bench::set (p, "reverb.bypass", 0.0f);
    p.removeModule (Module::reverb);
    REQUIRE (tailOf (p) == bare);
}

TEST_CASE ("The tail includes the delay's repeats until they are 60 dB down", "[tail]")
{
    ScratchpadProcessor p;
    const double bare = tailOf (p);

    p.addModule (Module::delay);
    bench::set (p, params::delayTime, 1000.0f);
    bench::set (p, params::delayFeedback, 50.0f);

    // log (0.001) / log (0.5) = 9.97 repeats of one second, plus the first echo.
    const double repeats = std::log (0.001) / std::log (0.5);
    REQUIRE (tailOf (p) - bare >= repeats);
    REQUIRE (tailOf (p) - bare < repeats + 2.5);

    bench::set (p, "delay.bypass", 1.0f);
    REQUIRE (tailOf (p) == bare);
}

TEST_CASE ("A delay that feeds back almost fully has a finite, capped tail", "[tail]")
{
    ScratchpadProcessor p;
    p.addModule (Module::delay);
    bench::set (p, params::delayTime, 2000.0f);
    bench::set (p, params::delayFeedback, 95.0f);

    const double tail = tailOf (p);
    REQUIRE (std::isfinite (tail));
    REQUIRE (tail <= 20.1);
    REQUIRE (tail >= 20.0);
}

TEST_CASE ("The tail is an upper bound on what is heard after the last note", "[tail]")
{
    {
        ScratchpadProcessor p;
        p.addModule (Module::ampEnv);
        bench::set (p, params::ampEnv.release, 1.5f);
        p.addModule (Module::reverb);
        bench::set (p, params::reverbDecay, 3.0f);
        p.addModule (Module::delay);
        bench::set (p, params::delayTime, 300.0f);
        bench::set (p, params::delayFeedback, 50.0f);
        bench::set (p, params::delayMix, 50.0f);

        const double reported = tailOf (p);
        const double heard = measuredTail (p);
        INFO ("reported " << reported << " s, heard " << heard << " s");
        REQUIRE (heard > 1.0);
        REQUIRE (reported >= heard);
    }
}
