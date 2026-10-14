#include "dsp/Envelope.h"
#include "dsp/FloatCompare.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

using scratchpad::dsp::Envelope;
using scratchpad::dsp::same;
using Stage = Envelope::Stage;

namespace
{
    constexpr double sampleRates[] = { 48000.0, 44100.0 };

    /** Timing may be off by this much (the brief's tolerance is 1 ms). */
    constexpr double timingTolerance = 0.001;

    // With every time at 20 ms the steepest segment (release or decay from
    // full scale) moves 1 - exp (-ln (10001) / 960) = 0.0095 per sample at
    // 48 kHz; the attack moves at most 1.3 * (1 - exp (-ln (13/3) / 960)) = 0.002.
    // A retrigger that restarted from zero would jump by 0.1 or more.
    constexpr float maxNormalStep = 0.012f;

    Envelope makeEnvelope (double sampleRate, float a, float d, float s, float r)
    {
        Envelope env;
        env.setSampleRate (sampleRate);
        env.setParams (a, d, s, r);
        return env;
    }

    /** Runs until `env` leaves `stage`; returns the number of samples taken, or -1 if it never does. */
    int samplesUntilLeaves (Envelope& env, Stage stage, int limit)
    {
        for (int i = 1; i <= limit; ++i)
        {
            env.next();
            if (env.getStage() != stage)
                return i;
        }
        return -1;
    }

    void runSamples (Envelope& env, int count)
    {
        for (int i = 0; i < count; ++i)
            env.next();
    }

    struct Steps
    {
        float maxUp = 0.0f;
        float maxDown = 0.0f;
        float maxLevel = 0.0f;
    };

    /** Runs `count` samples tracking the largest per-sample moves from `previous`. */
    Steps trackSteps (Envelope& env, int count, float previous)
    {
        Steps steps;
        for (int i = 0; i < count; ++i)
        {
            const float level = env.next();
            steps.maxUp = std::max (steps.maxUp, level - previous);
            steps.maxDown = std::max (steps.maxDown, previous - level);
            steps.maxLevel = std::max (steps.maxLevel, level);
            previous = level;
        }
        return steps;
    }
}

TEST_CASE ("A fresh or reset envelope is idle and outputs exactly zero", "[envelope]")
{
    Envelope env;
    env.setSampleRate (48000.0);

    for (int i = 0; i < 100; ++i)
        REQUIRE (same (env.next(), 0.0f));
    REQUIRE (env.isIdle());

    env.gateOn();
    runSamples (env, 500);
    REQUIRE_FALSE (env.isIdle());

    env.reset();
    REQUIRE (env.isIdle());
    REQUIRE (same (env.getLevel(), 0.0f));
    REQUIRE (same (env.next(), 0.0f));

    // gateOff on an idle envelope must not wake it.
    env.gateOff();
    REQUIRE (env.isIdle());
}

TEST_CASE ("Attack reaches full scale within 1 ms of the attack time", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float attack : { 0.001f, 0.01f, 0.1f, 1.0f })
        {
            auto env = makeEnvelope (sr, attack, 0.5f, 0.5f, 0.2f);
            env.gateOn();

            const int limit = (int) (sr * (attack + 0.1));
            float previous = 0.0f;
            int samples = -1;
            for (int i = 1; i <= limit; ++i)
            {
                const float level = env.next();
                REQUIRE (level >= previous);
                REQUIRE (level <= 1.0f);
                previous = level;

                if (env.getStage() != Stage::attack)
                {
                    samples = i;
                    break;
                }
            }

            INFO ("sr " << sr << " attack " << attack);
            REQUIRE (samples > 0);
            REQUIRE (std::abs ((double) samples / sr - (double) attack) <= timingTolerance);
            REQUIRE (same (previous, 1.0f));
        }
}

TEST_CASE ("Decay reaches the sustain level within 1 ms of the decay time", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float decay : { 0.05f, 0.2f })
            for (const float sustain : { 0.0f, 0.3f, 0.8f })
            {
                auto env = makeEnvelope (sr, 0.001f, decay, sustain, 0.2f);
                env.gateOn();
                REQUIRE (samplesUntilLeaves (env, Stage::attack, (int) sr) > 0);
                REQUIRE (env.getStage() == Stage::decay);

                float previous = env.getLevel();
                int samples = 0;
                const int limit = (int) (sr * (decay + 0.5));
                while (env.getStage() == Stage::decay && samples < limit)
                {
                    const float level = env.next();
                    REQUIRE (level <= previous);
                    REQUIRE (level >= sustain);
                    previous = level;
                    ++samples;
                }

                INFO ("sr " << sr << " decay " << decay << " sustain " << sustain);
                REQUIRE (env.getStage() != Stage::decay);
                REQUIRE (std::abs ((double) samples / sr - (double) decay) <= timingTolerance);
                REQUIRE (std::abs (env.getLevel() - sustain) < 0.002f);
            }
}

TEST_CASE ("Release from full scale reaches idle within 1 ms of the release time", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float release : { 0.002f, 0.05f, 0.5f })
        {
            auto env = makeEnvelope (sr, 0.001f, 0.01f, 1.0f, release);
            env.gateOn();
            runSamples (env, (int) (0.1 * sr));
            REQUIRE (env.getStage() == Stage::sustain);
            REQUIRE (env.getLevel() > 0.999f);

            env.gateOff();
            REQUIRE (env.getStage() == Stage::release);

            const int samples = samplesUntilLeaves (env, Stage::release, (int) (sr * (release + 1.0)));

            INFO ("sr " << sr << " release " << release);
            REQUIRE (samples > 0);
            REQUIRE (env.isIdle());
            REQUIRE (std::abs ((double) samples / sr - (double) release) <= timingTolerance);
            REQUIRE (same (env.getLevel(), 0.0f));
        }
}

TEST_CASE ("Release from a lower level is no later than the release time", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float release : { 0.05f, 0.5f })
            for (const float sustain : { 0.1f, 0.5f })
            {
                auto env = makeEnvelope (sr, 0.001f, 0.01f, sustain, release);
                env.gateOn();
                runSamples (env, (int) (0.2 * sr));
                REQUIRE (env.getStage() == Stage::sustain);

                env.gateOff();
                const int samples = samplesUntilLeaves (env, Stage::release, (int) (sr * (release + 1.0)));

                INFO ("sr " << sr << " release " << release << " sustain " << sustain);
                REQUIRE (samples > 0);
                REQUIRE ((double) samples / sr <= (double) release + timingTolerance);
            }
}

TEST_CASE ("Envelope times of 1 ms (and shorter) work", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float time : { 0.001f, 0.0f })
        {
            auto env = makeEnvelope (sr, time, time, 0.5f, time);
            env.gateOn();

            const int attack = samplesUntilLeaves (env, Stage::attack, (int) (0.01 * sr));
            REQUIRE (attack > 0);
            REQUIRE ((double) attack / sr <= 0.002);

            const int decay = samplesUntilLeaves (env, Stage::decay, (int) (0.01 * sr));
            REQUIRE (decay > 0);
            REQUIRE ((double) decay / sr <= 0.002);

            runSamples (env, 100);
            REQUIRE (std::abs (env.getLevel() - 0.5f) < 1.0e-3f);

            env.gateOff();
            const int release = samplesUntilLeaves (env, Stage::release, (int) (0.01 * sr));
            REQUIRE (release > 0);
            REQUIRE ((double) release / sr <= 0.002);
            REQUIRE (env.isIdle());
        }
}

TEST_CASE ("Retriggering during release continues from the current level", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        auto env = makeEnvelope (sr, 0.02f, 0.02f, 0.5f, 0.02f);
        env.gateOn();
        runSamples (env, (int) (0.3 * sr));
        env.gateOff();
        runSamples (env, (int) (0.001 * sr));

        const float before = env.getLevel();
        INFO ("sr " << sr);
        REQUIRE (before > 0.05f);
        REQUIRE (before < 0.45f);
        REQUIRE (env.getStage() == Stage::release);

        env.gateOn();
        const float first = env.next();
        REQUIRE (first >= before);
        REQUIRE (first - before <= maxNormalStep);
        const auto steps = trackSteps (env, (int) (0.2 * sr), first);

        REQUIRE (steps.maxUp <= maxNormalStep);
        REQUIRE (steps.maxDown <= maxNormalStep);
        REQUIRE (same (steps.maxLevel, 1.0f));
        REQUIRE (env.getStage() == Stage::sustain);
    }
}

TEST_CASE ("Retriggering during decay continues from the current level", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        auto env = makeEnvelope (sr, 0.02f, 0.1f, 0.2f, 0.02f);
        env.gateOn();
        REQUIRE (samplesUntilLeaves (env, Stage::attack, (int) sr) > 0);
        runSamples (env, (int) (0.005 * sr));

        const float before = env.getLevel();
        INFO ("sr " << sr);
        REQUIRE (env.getStage() == Stage::decay);
        REQUIRE (before > 0.4f);
        REQUIRE (before < 0.95f);

        env.gateOn();
        const float first = env.next();
        REQUIRE (first >= before);
        REQUIRE (first - before <= maxNormalStep);
        const auto steps = trackSteps (env, (int) (0.4 * sr), first);

        REQUIRE (steps.maxUp <= maxNormalStep);
        REQUIRE (steps.maxDown <= maxNormalStep);
        REQUIRE (same (steps.maxLevel, 1.0f));
        REQUIRE (env.getStage() == Stage::sustain);
    }
}

TEST_CASE ("Retriggering during attack does not disturb the attack", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        auto env = makeEnvelope (sr, 0.05f, 0.02f, 0.5f, 0.02f);
        auto reference = env;
        env.gateOn();
        reference.gateOn();
        runSamples (env, (int) (0.02 * sr));
        runSamples (reference, (int) (0.02 * sr));

        REQUIRE (env.getStage() == Stage::attack);
        REQUIRE (env.getLevel() > 0.1f);

        env.gateOn();
        for (int i = 0; i < (int) (0.5 * sr); ++i)
            REQUIRE (same (env.next(), reference.next()));
    }
}

TEST_CASE ("Note-off during the attack releases from the current level", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        const float release = 0.03f;
        auto env = makeEnvelope (sr, 0.05f, 0.1f, 0.7f, release);
        env.gateOn();
        runSamples (env, (int) (0.02 * sr));

        const float before = env.getLevel();
        INFO ("sr " << sr);
        REQUIRE (env.getStage() == Stage::attack);
        REQUIRE (before > 0.1f);
        REQUIRE (before < 0.9f);

        env.gateOff();
        REQUIRE (env.getStage() == Stage::release);

        float previous = before;
        float maxStepDown = 0.0f;
        int samples = 0;
        while (! env.isIdle() && samples < (int) (sr * 0.5))
        {
            const float level = env.next();
            REQUIRE (level <= previous);
            maxStepDown = std::max (maxStepDown, previous - level);
            previous = level;
            ++samples;
        }

        REQUIRE (env.isIdle());
        REQUIRE (maxStepDown <= maxNormalStep);
        REQUIRE ((double) samples / sr <= (double) release + timingTolerance);
    }
}

TEST_CASE ("The sustain stage holds the sustain level and follows changes smoothly", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        auto env = makeEnvelope (sr, 0.001f, 0.01f, 0.6f, 0.1f);
        env.gateOn();
        runSamples (env, (int) (0.2 * sr));
        REQUIRE (env.getStage() == Stage::sustain);

        for (int i = 0; i < (int) (0.5 * sr); ++i)
            REQUIRE (std::abs (env.next() - 0.6f) < 1.0e-5f);

        // One step of the 5 ms smoothing covers 1 - exp (-1 / (0.005 sr)) of the remaining distance.
        const float smoothing = 1.02f * (1.0f - std::exp (-1.0f / (0.005f * (float) sr)));
        env.setParams (0.001f, 0.01f, 0.2f, 0.1f);
        auto steps = trackSteps (env, (int) (0.1 * sr), env.getLevel());
        REQUIRE (steps.maxUp <= 0.0f);
        REQUIRE (steps.maxDown <= 0.4f * smoothing);
        REQUIRE (std::abs (env.getLevel() - 0.2f) < 1.0e-3f);
        REQUIRE (env.getStage() == Stage::sustain);

        env.setParams (0.001f, 0.01f, 0.9f, 0.1f);
        steps = trackSteps (env, (int) (0.1 * sr), env.getLevel());
        REQUIRE (steps.maxDown <= 0.0f);
        REQUIRE (steps.maxUp <= 0.7f * smoothing);
        REQUIRE (std::abs (env.getLevel() - 0.9f) < 1.0e-3f);
    }
}

TEST_CASE ("Changing the sustain level during the decay does not make the level jump", "[envelope]")
{
    for (const double sr : sampleRates)
        for (const float newSustain : { 0.0f, 0.9f })
        {
            auto env = makeEnvelope (sr, 0.001f, 0.1f, 0.3f, 0.05f);
            env.gateOn();
            REQUIRE (samplesUntilLeaves (env, Stage::attack, (int) sr) > 0);
            runSamples (env, (int) (0.005 * sr));
            REQUIRE (env.getStage() == Stage::decay);

            const float before = env.getLevel();
            REQUIRE (before > 0.5f);
            REQUIRE (before < 0.85f);

            // The decay of 0.1 s spans 0.7 here, so its natural step is ~0.002.
            env.setParams (0.001f, 0.1f, newSustain, 0.05f);
            const auto steps = trackSteps (env, (int) (0.5 * sr), before);

            INFO ("sr " << sr << " new sustain " << newSustain);
            REQUIRE (steps.maxUp <= maxNormalStep);
            REQUIRE (steps.maxDown <= maxNormalStep);
            REQUIRE (env.getStage() == Stage::sustain);
            REQUIRE (std::abs (env.getLevel() - newSustain) < 1.0e-3f);
        }
}

TEST_CASE ("Envelope level stays in [0, 1] and ends exactly at zero when idle", "[envelope]")
{
    for (const double sr : sampleRates)
    {
        auto env = makeEnvelope (sr, 0.003f, 0.01f, 0.5f, 0.02f);
        std::uint32_t rng = 12345u;
        const auto random = [&rng]
        {
            rng = rng * 1664525u + 1013904223u;
            return (float) (rng >> 8) / (float) (1u << 24);
        };

        for (int block = 0; block < 400; ++block)
        {
            const float choice = random();
            if (choice < 0.3f)
                env.gateOn();
            else if (choice < 0.6f)
                env.gateOff();
            else if (choice < 0.8f)
                env.setParams (0.0005f + random() * 0.05f, 0.0005f + random() * 0.05f, random(), 0.0005f + random() * 0.05f);

            for (int i = 0; i < 200; ++i)
            {
                const float level = env.next();
                REQUIRE (std::isfinite (level));
                REQUIRE (level >= 0.0f);
                REQUIRE (level <= 1.0f);
            }
        }

        env.gateOff();
        const int limit = (int) (sr * 2.0);
        for (int i = 0; i < limit && ! env.isIdle(); ++i)
            env.next();

        REQUIRE (env.isIdle());
        for (int i = 0; i < 100; ++i)
            REQUIRE (same (env.next(), 0.0f));
        REQUIRE (same (env.getLevel(), 0.0f));
    }
}
