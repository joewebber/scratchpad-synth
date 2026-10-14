#include "engine/VoiceAllocator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <vector>

using namespace scratchpad;
using engine::Voice;
using engine::VoiceAllocator;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double bend = 0.0;

    VoiceAllocator makeAllocator (int voices)
    {
        VoiceAllocator a;
        a.prepare (sampleRate);
        a.setActiveVoices (voices);
        return a;
    }

    void run (VoiceAllocator& a, int numSamples)
    {
        std::vector<float> scratch ((size_t) numSamples, 0.0f);
        a.render (scratch.data(), numSamples, dsp::Wave::sine, bend);
    }

    int milliseconds (double ms) { return (int) (ms * 0.001 * sampleRate) + 2; }

    const Voice* findNote (const VoiceAllocator& a, int note)
    {
        for (const auto& v : a.getVoices())
            if (v.state != Voice::State::idle && v.note == note)
                return &v;
        return nullptr;
    }

    const Voice* findPending (const VoiceAllocator& a, int note)
    {
        for (const auto& v : a.getVoices())
            if (v.state == Voice::State::stealing && v.hasPending && v.pending.note == note)
                return &v;
        return nullptr;
    }
}

TEST_CASE ("Free voices are used before anything is stolen", "[voices]")
{
    auto a = makeAllocator (4);

    for (int n : { 60, 62, 64 })
        a.noteOn (n, 0.8f, false, dsp::Wave::sine, bend);

    REQUIRE (a.countActive() == 3);
    for (const auto& v : a.getVoices())
        REQUIRE (v.state != Voice::State::stealing);
}

TEST_CASE ("Stealing takes the oldest released voice first", "[voices]")
{
    auto a = makeAllocator (4);

    for (int n : { 60, 61, 62, 63 })
        a.noteOn (n, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 64);

    // 62 is released first, then 60; 61 and 63 stay held.
    a.noteOff (62, false);
    a.noteOff (60, false);
    // Not enough time has passed for either release (2 ms) to finish.
    run (a, 8);

    a.noteOn (70, 0.8f, false, dsp::Wave::sine, bend);

    REQUIRE (findPending (a, 70) != nullptr);
    REQUIRE (findPending (a, 70)->note == 62); // the oldest release, not the oldest note
}

TEST_CASE ("With no released voices, stealing takes the oldest held voice", "[voices]")
{
    auto a = makeAllocator (3);

    for (int n : { 64, 60, 67 })
    {
        a.noteOn (n, 0.8f, false, dsp::Wave::sine, bend);
        run (a, 64);
    }

    a.noteOn (72, 0.8f, false, dsp::Wave::sine, bend);

    REQUIRE (findPending (a, 72) != nullptr);
    REQUIRE (findPending (a, 72)->note == 64); // played first, even though 60 is lower
}

TEST_CASE ("A stolen voice fades over 5 ms, then the new note starts", "[voices]")
{
    auto a = makeAllocator (1);

    a.noteOn (60, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 2000);

    a.noteOn (72, 0.8f, false, dsp::Wave::sine, bend);
    REQUIRE (findNote (a, 60) != nullptr);
    REQUIRE (a.getVoices()[0].state == Voice::State::stealing);

    run (a, milliseconds (4.0));
    REQUIRE (a.getVoices()[0].note == 60); // still fading out

    run (a, milliseconds (2.0));
    REQUIRE (a.getVoices()[0].note == 72);
    REQUIRE (a.getVoices()[0].state == Voice::State::held);
}

TEST_CASE ("Releasing the pending note cancels it", "[voices]")
{
    auto a = makeAllocator (1);

    a.noteOn (60, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 2000);
    a.noteOn (72, 0.8f, false, dsp::Wave::sine, bend);
    a.noteOff (72, false);

    run (a, milliseconds (10.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("A released voice becomes free after its 2 ms fade", "[voices]")
{
    auto a = makeAllocator (2);

    a.noteOn (60, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 2000);
    a.noteOff (60, false);

    REQUIRE (a.countActive() == 1);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("The sustain pedal holds released keys until it is lifted", "[voices]")
{
    auto a = makeAllocator (2);

    a.setSustain (true);
    a.noteOn (60, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 2000);
    a.noteOff (60, false);
    run (a, 2000);
    REQUIRE (a.countActive() == 1);
    REQUIRE (findNote (a, 60)->state == Voice::State::held);

    a.setSustain (false);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("Preview notes and played notes with the same number are independent", "[voices]")
{
    auto a = makeAllocator (4);

    a.noteOn (57, 0.8f, true, dsp::Wave::saw, bend);
    a.noteOn (57, 0.8f, false, dsp::Wave::sine, bend);
    run (a, 256);
    REQUIRE (a.countActive() == 2);

    a.noteOff (57, true);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 1);
    REQUIRE_FALSE (findNote (a, 57)->preview);
}
