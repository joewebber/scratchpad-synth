#pragma once

#include "dsp/BlepTable.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace scratchpad::dsp
{

/** The four waves a user can pick. The integer values are stored in saved
    patches (the `osc1.wave` choice index), so never reorder them. */
enum class Wave
{
    sine = 0,
    triangle = 1,
    saw = 2,
    pulse = 3
};

inline constexpr int numWaves = 4;

namespace detail
{
    /** Naive waveform plus band-limited corrections for one wave shape.

        Each call generates one naive sample at phase `p` and adds the BLEP
        (for jumps) or BLAMP (for corners) residual of any edge between the
        previous sample and this one into a ring of `BlepTable::width`
        samples. What comes back is the sample `BlepTable::halfWidth` samples
        ago, by which time every correction that touches it has been added. */
    class BandLimitedShape
    {
    public:
        enum class Kind { triangle, saw, pulse };

        explicit BandLimitedShape (Kind k) noexcept : kind (k) {}

        void clear() noexcept
        {
            ring.fill (0.0f);
            pos = 0;
        }

        /** `detectEdges` is false for the very first sample of a note, which
            starts from silence rather than from a wrap. */
        float tick (double p, double dt, double pulseWidth, bool detectEdges = true) noexcept
        {
            const auto& table = BlepTable::get();

            switch (kind)
            {
                case Kind::saw:
                    if (detectEdges)
                        addEdge (table, false, p, dt, 0.0, -2.0);
                    ring[pos] += (float) (2.0 * p - 1.0);
                    break;

                case Kind::pulse:
                    if (detectEdges)
                    {
                        addEdge (table, false, p, dt, 0.0, 2.0);
                        addEdge (table, false, p, dt, pulseWidth, -2.0);
                    }
                    ring[pos] += p < pulseWidth ? 1.0f : -1.0f;
                    break;

                case Kind::triangle:
                    // The slope is +-4 per cycle, so each corner changes it by +-8.
                    if (detectEdges)
                    {
                        addEdge (table, true, p, dt, 0.0, 8.0 * dt);
                        addEdge (table, true, p, dt, 0.5, -8.0 * dt);
                    }
                    ring[pos] += p < 0.5 ? (float) (4.0 * p - 1.0) : (float) (3.0 - 4.0 * p);
                    break;
            }

            const size_t out = (pos + BlepTable::halfWidth) & mask;
            const float y = ring[out];
            ring[out] = 0.0f;
            pos = (pos + 1) & mask;
            return y;
        }

        /** Fills the ring as if this shape had been running for the last
            `halfWidth` samples, so a shape switched on mid-note arrives in
            step with the phase instead of leaving a gap. */
        void prime (double phaseNow, double dt, double pulseWidth) noexcept
        {
            clear();

            double p = phaseNow - (double) BlepTable::halfWidth * dt;
            p -= std::floor (p);

            for (int i = 0; i < BlepTable::halfWidth; ++i)
            {
                tick (p, dt, pulseWidth);
                p += dt;
                if (p >= 1.0)
                    p -= 1.0;
            }
        }

    private:
        static constexpr size_t mask = BlepTable::width - 1;
        static_assert ((BlepTable::width & (BlepTable::width - 1)) == 0, "ring size must be a power of two");

        /** If the waveform passes phase `edge` between the previous sample and
            this one, adds a step of `amount` there (`ramp`: a slope change of
            `amount` per sample instead). */
        void addEdge (const BlepTable& table, bool ramp, double p, double dt, double edge, double amount) noexcept
        {
            double sinceEdge = p - edge;
            if (sinceEdge < 0.0)
                sinceEdge += 1.0;

            if (sinceEdge >= dt)
                return;

            // The edge was `fraction` of a sample before this sample.
            const double scaled = sinceEdge / dt * BlepTable::oversampling;
            const int row = (int) scaled;
            const float blend = (float) (scaled - (double) row);

            const float* a = ramp ? table.rampRow (row) : table.stepRow (row);
            const float* b = ramp ? table.rampRow (row + 1) : table.stepRow (row + 1);
            const float gain = (float) amount;

            // Tap j lands on the sample (j - halfWidth) after this one.
            size_t slot = (pos + BlepTable::halfWidth) & mask;
            for (int j = 0; j < BlepTable::width; ++j)
            {
                ring[slot] += gain * (a[j] + (b[j] - a[j]) * blend);
                slot = (slot + 1) & mask;
            }
        }

        Kind kind;
        std::array<float, BlepTable::width> ring {};
        size_t pos = 0;
    };
}

/** Band-limited oscillator with a double-precision phase accumulator.

    Saw and pulse use a windowed-sinc BLEP and triangle a matching BLAMP, so
    harmonics that would land above Nyquist are removed instead of aliasing
    back down. Sine is direct.

    Everything runs `latencySamples` late (the BLEP kernel is symmetric), so
    the first `latencySamples` samples after `reset` are silent and then the
    wave starts at its start phase. Waves are aligned with each other, which
    is what lets two of them be crossfaded. */
class Oscillator
{
public:
    static constexpr int latencySamples = BlepTable::halfWidth;

    void setFrequency (double hz, double sampleRate) noexcept { dt = hz / sampleRate; }

    /** Sets the per-sample phase increment directly (cycles per sample). */
    void setIncrement (double cyclesPerSample) noexcept { dt = cyclesPerSample; }

    void setPulseWidth (double width) noexcept { pulseWidth = width; }

    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase;
        startupRemaining = latencySamples;
        fresh = true;
        active.fill (false);
        for (auto& s : shapes)
            s.clear();
    }

    double getPhase() const noexcept { return phase; }

    float process (Wave wave) noexcept
    {
        used.fill (false);
        const float y = run (wave);
        finishSample();
        return y;
    }

    /** `a` and `b` evaluated at the same phase and mixed: `mix` 0 is all `a`,
        1 is all `b`. */
    float process (Wave a, Wave b, float mix) noexcept
    {
        used.fill (false);
        const float ya = run (a);
        const float yb = run (b);
        finishSample();
        return ya + (yb - ya) * mix;
    }

private:
    static constexpr int numShapes = 3;

    float run (Wave wave) noexcept
    {
        if (wave == Wave::sine)
        {
            if (startupRemaining > 0)
                return 0.0f;

            // Same delay as the band-limited shapes, as a phase offset.
            return (float) std::sin (2.0 * std::numbers::pi * (phase - (double) latencySamples * dt));
        }

        const size_t index = wave == Wave::triangle ? 0 : wave == Wave::saw ? 1 : 2;
        auto& shape = shapes[index];

        if (! active[index])
        {
            // Joining mid-note needs history; at the start of a note the
            // shape starts empty and the output is silent until it fills.
            if (! fresh)
                shape.prime (phase, dt, pulseWidth);

            active[index] = true;
        }

        used[index] = true;
        return shape.tick (phase, dt, pulseWidth, ! fresh);
    }

    void finishSample() noexcept
    {
        for (size_t i = 0; i < (size_t) numShapes; ++i)
            if (! used[i])
                active[i] = false;

        if (startupRemaining > 0)
            --startupRemaining;

        fresh = false;
        phase += dt;
        if (phase >= 1.0)
            phase -= std::floor (phase);
    }

    using Shape = detail::BandLimitedShape;

    std::array<Shape, numShapes> shapes {
        Shape (Shape::Kind::triangle), Shape (Shape::Kind::saw), Shape (Shape::Kind::pulse)
    };
    std::array<bool, numShapes> active {};
    std::array<bool, numShapes> used {};

    double phase = 0.0;
    double dt = 0.0;
    double pulseWidth = 0.5;
    int startupRemaining = latencySamples;
    bool fresh = true;
};

} // namespace scratchpad::dsp
