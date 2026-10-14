#pragma once

#include <algorithm>
#include <cmath>

namespace scratchpad::dsp
{

/** Analogue-style ADSR with exponential segments.

    Timing: the attack reaches 1.0 after `attack` seconds. Decay reaches the
    sustain level after `decay` seconds. Release takes `release` seconds to
    fall from full scale to silence (from a lower level it gets there
    sooner, because each segment is the same exponential curve). Segments
    aim a little past their destination (`targetRatio`) so they arrive in
    finite time instead of creeping up to it.

    Retriggering starts the attack from wherever the level is, never from
    zero, so there is no click. Changing a time or the sustain level while a
    note is playing re-aims the running segment without a jump. */
class Envelope
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    void setSampleRate (double sr) noexcept
    {
        sampleRate = sr;
        sustainSmoothing = 1.0 - std::exp (-1.0 / (0.005 * sr)); // 5 ms
        recalculate();
    }

    /** Seconds for the three times; sustain is a level, 0 to 1. */
    void setParams (float attackSeconds, float decaySeconds, float sustainLevel, float releaseSeconds) noexcept
    {
        attackTime = std::max (attackSeconds, 0.0005f);
        decayTime = std::max (decaySeconds, 0.0005f);
        releaseTime = std::max (releaseSeconds, 0.0005f);
        sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
        recalculate();
    }

    void gateOn() noexcept
    {
        stage = Stage::attack;
    }

    void gateOff() noexcept
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }

    void reset() noexcept
    {
        stage = Stage::idle;
        level = 0.0;
    }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                break;

            case Stage::attack:
                level = attackBase + level * attackCoef;
                if (level >= 1.0)
                {
                    level = 1.0;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
                level = decayBase + level * decayCoef;
                if (level <= (double) sustain)
                {
                    // Arriving overshoots the destination by a hair, which is
                    // snapped away. A sustain raised during the decay leaves a
                    // real gap, which the sustain stage closes smoothly.
                    if ((double) sustain - level <= 2.0 * fallRatio)
                        level = sustain;

                    stage = Stage::sustain;
                }
                break;

            case Stage::sustain:
                level += ((double) sustain - level) * sustainSmoothing;
                break;

            case Stage::release:
                level = releaseBase + level * releaseCoef;
                if (level <= silence)
                {
                    level = 0.0;
                    stage = Stage::idle;
                }
                break;
        }

        return (float) level;
    }

    float getLevel() const noexcept { return (float) level; }
    Stage getStage() const noexcept { return stage; }
    bool isIdle() const noexcept { return stage == Stage::idle; }

private:
    // Each segment is an exponential aimed `ratio` past its destination, so it
    // crosses the destination in finite time. `span` is the distance from the
    // segment's start to its destination; `samples` is how long that takes.
    // Doubles throughout: a 10 s attack has a per-sample step near 1e-5.
    static constexpr double attackRatio = 0.3;    // how far past 1.0 the attack aims
    static constexpr double fallRatio = 0.0001;   // how far past the destination decay and release aim
    static constexpr double silence = 1.0e-4;     // release ends below this (-80 dB)

    static double coefficient (double samples, double span, double ratio) noexcept
    {
        return std::exp (-std::log ((span + ratio) / ratio) / std::max (samples, 1.0));
    }

    void recalculate() noexcept
    {
        attackCoef = coefficient (attackTime * sampleRate, 1.0, attackRatio);
        attackBase = (1.0 + attackRatio) * (1.0 - attackCoef);

        // Decay runs from 1 down to the sustain level.
        decayCoef = coefficient (decayTime * sampleRate, 1.0 - (double) sustain, fallRatio);
        decayBase = ((double) sustain - fallRatio) * (1.0 - decayCoef);

        // Release runs from full scale down to `silence`.
        releaseCoef = std::exp (-std::log ((1.0 + fallRatio) / (silence + fallRatio)) / std::max (releaseTime * sampleRate, 1.0));
        releaseBase = -fallRatio * (1.0 - releaseCoef);
    }

    double sampleRate = 48000.0;
    float attackTime = 0.005f, decayTime = 0.3f, releaseTime = 0.2f, sustain = 0.8f;
    double attackCoef = 0, attackBase = 0, decayCoef = 0, decayBase = 0, releaseCoef = 0, releaseBase = 0;
    double sustainSmoothing = 0.01;
    double level = 0.0;
    Stage stage = Stage::idle;
};

} // namespace scratchpad::dsp
