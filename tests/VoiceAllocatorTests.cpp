#include "engine/VoiceAllocator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <vector>

using namespace scratchpad;
using engine::BlockLevels;
using engine::EngineParams;
using engine::Voice;
using engine::VoiceAllocator;
using engine::VoiceMode;

namespace
{
    constexpr double sampleRate = 48000.0;

    const EngineParams& patch()
    {
        static const EngineParams p = [] {
            EngineParams e;
            e.osc[0].enabled = true;
            e.osc[0].wave = dsp::Wave::sine;
            e.osc[0].levelDb = 0.0f;
            return e;
        }();

        return p;
    }

    void prepared (VoiceAllocator& a, int voices)
    {
        a.prepare (sampleRate);
        a.setActiveVoices (voices);
    }

    void run (VoiceAllocator& a, int numSamples)
    {
        const auto levels = BlockLevels::steady (patch());
        std::vector<float> scratch ((size_t) Voice::blockSize, 0.0f);

        for (int done = 0; done < numSamples; done += Voice::blockSize)
        {
            const int n = std::min (Voice::blockSize, numSamples - done);
            a.render (scratch.data(), n, patch(), levels, 0.0);
        }
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
    VoiceAllocator a;
    prepared (a, 4);

    for (int n : { 60, 62, 64 })
        a.noteOn (n, 0.8f);

    REQUIRE (a.countActive() == 3);
    for (const auto& v : a.getVoices())
        REQUIRE (v.state != Voice::State::stealing);
}

TEST_CASE ("The voice limit caps how many notes sound at once", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 3);

    for (int n : { 60, 62, 64, 65, 67 })
    {
        a.noteOn (n, 0.8f);
        run (a, 64);
    }
    run (a, 1000); // let the steals finish

    REQUIRE (a.countActive() == 3);
    // The two oldest notes were taken over by the newest two.
    REQUIRE (findNote (a, 60) == nullptr);
    REQUIRE (findNote (a, 62) == nullptr);
    for (int n : { 64, 65, 67 })
        REQUIRE (findNote (a, n) != nullptr);
}

TEST_CASE ("Lowering the voice limit lets the extra voices finish", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 4);

    for (int n : { 60, 62, 64, 65 })
        a.noteOn (n, 0.8f);
    run (a, 256);

    a.setActiveVoices (2);
    run (a, milliseconds (2.0));

    REQUIRE (a.countActive() == 2);
    REQUIRE (findNote (a, 60) != nullptr);
    REQUIRE (findNote (a, 62) != nullptr);
}

TEST_CASE ("Stealing takes the oldest released voice first", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 4);

    for (int n : { 60, 61, 62, 63 })
        a.noteOn (n, 0.8f);
    run (a, 64);

    // 62 is released first, then 60; 61 and 63 stay held.
    a.noteOff (62);
    a.noteOff (60);
    // Not enough time has passed for either release (2 ms) to finish.
    run (a, 8);

    a.noteOn (70, 0.8f);

    REQUIRE (findPending (a, 70) != nullptr);
    REQUIRE (findPending (a, 70)->note == 62); // the oldest release, not the oldest note
}

TEST_CASE ("With no released voices, stealing takes the oldest held voice", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 3);

    for (int n : { 64, 60, 67 })
    {
        a.noteOn (n, 0.8f);
        run (a, 64);
    }

    a.noteOn (72, 0.8f);

    REQUIRE (findPending (a, 72) != nullptr);
    REQUIRE (findPending (a, 72)->note == 64); // played first, even though 60 is lower
}

TEST_CASE ("A stolen voice fades over 5 ms, then the new note starts", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 1);

    a.noteOn (60, 0.8f);
    run (a, 2000);

    a.noteOn (72, 0.8f);
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
    VoiceAllocator a;
    prepared (a, 1);

    a.noteOn (60, 0.8f);
    run (a, 2000);
    a.noteOn (72, 0.8f);
    a.noteOff (72);

    run (a, milliseconds (10.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("A released voice becomes free after its 2 ms fade", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 2);

    a.noteOn (60, 0.8f);
    run (a, 2000);
    a.noteOff (60);

    REQUIRE (a.countActive() == 1);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("The sustain pedal holds released keys until it is lifted", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 2);

    a.setSustain (true);
    a.noteOn (60, 0.8f);
    run (a, 2000);
    a.noteOff (60);
    run (a, 2000);
    REQUIRE (a.countActive() == 1);
    REQUIRE (findNote (a, 60)->state == Voice::State::held);

    a.setSustain (false);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("Preview notes and played notes with the same number are independent", "[voices]")
{
    VoiceAllocator a;
    prepared (a, 4);

    a.noteOn (57, 0.8f, true, dsp::Wave::saw);
    a.noteOn (57, 0.8f);
    run (a, 256);
    REQUIRE (a.countActive() == 2);

    a.noteOff (57, true);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 1);
    REQUIRE_FALSE (findNote (a, 57)->preview);
}

TEST_CASE ("Mono and legato play every note on one voice", "[voices][voicing]")
{
    for (auto mode : { VoiceMode::mono, VoiceMode::legato })
    {
        VoiceAllocator a;
        prepared (a, 8);

        engine::VoicingParams v;
        v.mode = mode;
        a.setVoicing (v);

        for (int n : { 60, 64, 67 })
        {
            a.noteOn (n, 0.8f);
            run (a, 64);
        }

        REQUIRE (a.countActive() == 1);
        REQUIRE (findNote (a, 67) != nullptr);
    }
}

TEST_CASE ("Releasing the sounding mono note returns to the most recent held one", "[voices][voicing]")
{
    VoiceAllocator a;
    prepared (a, 8);

    engine::VoicingParams v;
    v.mode = VoiceMode::mono;
    a.setVoicing (v);

    for (int n : { 60, 64, 67 })
    {
        a.noteOn (n, 0.8f);
        run (a, 64);
    }

    a.noteOff (64); // not sounding: nothing changes
    run (a, 64);
    REQUIRE (findNote (a, 67) != nullptr);

    a.noteOff (67); // back to 60, the only key still down
    run (a, 64);
    REQUIRE (a.countActive() == 1);
    REQUIRE (findNote (a, 60)->state == Voice::State::held);

    a.noteOff (60);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("Changing the voicing mode releases played notes", "[voices][voicing]")
{
    VoiceAllocator a;
    prepared (a, 8);

    for (int n : { 60, 64, 67 })
        a.noteOn (n, 0.8f);
    run (a, 256);

    engine::VoicingParams v;
    v.mode = VoiceMode::mono;
    a.setVoicing (v);

    for (const auto& voice : a.getVoices())
        REQUIRE (voice.state != Voice::State::held);

    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 0);
}

TEST_CASE ("Previews use poly allocation in mono mode and leave the key stack alone", "[voices][voicing]")
{
    VoiceAllocator a;
    prepared (a, 8);

    engine::VoicingParams v;
    v.mode = VoiceMode::mono;
    a.setVoicing (v);

    a.noteOn (60, 0.8f);
    a.noteOn (57, 0.8f, true, dsp::Wave::saw);
    a.noteOn (59, 0.8f, true, dsp::Wave::pulse);
    run (a, 256);
    REQUIRE (a.countActive() == 3);

    a.noteOff (57, true);
    a.noteOff (59, true);
    run (a, milliseconds (2.0));
    REQUIRE (a.countActive() == 1);
    REQUIRE (findNote (a, 60)->state == Voice::State::held);
}
