#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace scratchpad::dsp
{

/** Choice order matches `params::lfoShapeNames()`. */
enum class LfoShape { sine = 0, triangle, sawUp, sawDown, square, sampleHold };

inline constexpr int numLfoShapes = 6;

/** Slowest and fastest free-running rate. */
inline constexpr double lfoMinHz = 0.01;
inline constexpr double lfoMaxHz = 50.0;

/** A low frequency oscillator: a phase accumulator read at the end of each
    block. The output is bipolar, -1 to +1.

    Every shape starts a cycle at phase 0: sine and triangle at 0 and rising,
    saw up at -1, saw down and square at +1. Sample and hold draws a new value
    (white, from an xorshift generator) at each wrap and holds it for the cycle. */
class Lfo
{
public:
    void setSampleRate (double sr) noexcept { sampleRate = sr; }
    void seed (std::uint32_t s) noexcept { rng = s == 0 ? 0x9e3779b9u : s; }
    void setShape (LfoShape s) noexcept { shape = s; }
    void setRate (double hz) noexcept { increment = std::clamp (hz, lfoMinHz, lfoMaxHz) / sampleRate; }

    /** Restarts the cycle at `startPhase` (0 .. 1). */
    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase - std::floor (startPhase);
        held = draw();
    }

    /** Moves the phase on by `numSamples` and returns the value there. */
    float advance (int numSamples) noexcept
    {
        phase += increment * (double) numSamples;

        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = draw();
        }

        return value();
    }

    float value() const noexcept
    {
        const double p = phase;

        switch (shape)
        {
            case LfoShape::sine:       return (float) std::sin (2.0 * std::numbers::pi * p);
            case LfoShape::triangle:   return (float) (p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
            case LfoShape::sawUp:      return (float) (2.0 * p - 1.0);
            case LfoShape::sawDown:    return (float) (1.0 - 2.0 * p);
            case LfoShape::square:     return p < 0.5 ? 1.0f : -1.0f;
            case LfoShape::sampleHold: return held;
        }

        return 0.0f;
    }

    double getPhase() const noexcept { return phase; }

private:
    float draw() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return (float) ((double) rng / 2147483648.0 - 1.0);
    }

    double sampleRate = 48000.0;
    double increment = 2.0 / 48000.0;
    double phase = 0.0;
    float held = 0.0f;
    std::uint32_t rng = 0x9e3779b9u;
    LfoShape shape = LfoShape::sine;
};

} // namespace scratchpad::dsp
