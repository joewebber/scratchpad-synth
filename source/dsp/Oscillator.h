#pragma once

#include "dsp/FastFloor.h"
#include "dsp/BlepTable.h"

#include <algorithm>
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

/** The naive (not band-limited) wave at phase `p` (0 .. 1). */
inline double naiveWave (Wave wave, double p, double pulseWidth) noexcept
{
    switch (wave)
    {
        case Wave::sine:     return std::sin (2.0 * std::numbers::pi * p);
        case Wave::triangle: return p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p;
        case Wave::saw:      return 2.0 * p - 1.0;
        case Wave::pulse:    return p < pulseWidth ? 1.0 : -1.0;
    }
    return 0.0;
}

/** Slope of the naive wave at phase `p`, per cycle (the jumps are not part of it). */
inline double naiveSlope (Wave wave, double p) noexcept
{
    switch (wave)
    {
        case Wave::sine:     return 2.0 * std::numbers::pi * std::cos (2.0 * std::numbers::pi * p);
        case Wave::triangle: return p < 0.5 ? 4.0 : -4.0;
        case Wave::saw:      return 2.0;
        case Wave::pulse:    return 0.0;
    }
    return 0.0;
}

namespace detail
{
    /** Naive waveform plus band-limited corrections for one wave shape.

        Each call generates one naive sample at phase `p` and adds the BLEP
        (for jumps) or BLAMP (for corners) residual of any edge between the
        previous sample and this one into a ring of `BlepTable::width`
        samples. What comes back is the sample `BlepTable::halfWidth` samples
        ago, by which time every correction that touches it has been added.

        The corrections are scaled by `bandLimit` (1 is the full effect, 0
        leaves the naive wave): the ring is a plain delay either way, so
        fading `bandLimit` crossfades between the naive and the band-limited
        wave without a click. The sine has no edges of its own; it goes
        through the ring so that it is delayed like the others and so that a
        hard-sync reset can be corrected. */
    class BandLimitedShape
    {
    public:
        enum class Kind { triangle, saw, pulse, sine };

        /** A hard-sync restart inside the sample being generated: the cycle
            began again `fraction` of a sample before it, when it stood at `oldPhase`. */
        struct Reset
        {
            double fraction = 0.0;
            double oldPhase = 0.0;
        };

        explicit BandLimitedShape (Kind k) noexcept : kind (k) {}

        void clear() noexcept
        {
            ring.fill (0.0f);
            pos = 0;
        }

        /** `detectEdges` is false for the very first sample of a note, which
            starts from silence rather than from a wrap. `dt` is the phase
            advance since the previous sample; it is negative when frequency
            modulation makes the phase run backwards. */
        float tick (double p, double dt, double pulseWidth, bool detectEdges = true, float bandLimit = 1.0f, const Reset* reset = nullptr) noexcept
        {
            if (detectEdges && bandLimit > 0.0f && dt != 0.0 && std::abs (dt) < 0.5)
            {
                const auto& table = BlepTable::get();

                if (reset != nullptr)
                    addReset (table, p, dt, pulseWidth, bandLimit, *reset);
                else
                    addEdges (table, p, dt, std::abs (dt), 0.0, pulseWidth, bandLimit, false);
            }

            switch (kind)
            {
                case Kind::saw:
                    ring[pos] += (float) (2.0 * p - 1.0);
                    break;

                case Kind::pulse:
                    ring[pos] += p < pulseWidth ? 1.0f : -1.0f;
                    break;

                case Kind::triangle:
                    ring[pos] += p < 0.5 ? (float) (4.0 * p - 1.0) : (float) (3.0 - 4.0 * p);
                    break;

                case Kind::sine:
                    ring[pos] += (float) std::sin (2.0 * std::numbers::pi * p);
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
        void prime (double phaseNow, double dt, double pulseWidth, float bandLimit = 1.0f) noexcept
        {
            clear();

            double p = phaseNow - (double) BlepTable::halfWidth * dt;
            p -= fastFloor (p);

            for (int i = 0; i < BlepTable::halfWidth; ++i)
            {
                tick (p, dt, pulseWidth, true, bandLimit);
                p += dt;
                if (p >= 1.0)
                    p -= 1.0;
            }
        }

    private:
        static constexpr size_t mask = BlepTable::width - 1;
        static_assert ((BlepTable::width & (BlepTable::width - 1)) == 0, "ring size must be a power of two");

        Wave waveOf() const noexcept
        {
            switch (kind)
            {
                case Kind::triangle: return Wave::triangle;
                case Kind::saw:      return Wave::saw;
                case Kind::pulse:    return Wave::pulse;
                case Kind::sine:     break;
            }
            return Wave::sine;
        }

        /** Every edge of the wave that the phase passed on its way to `p`.
            `segment` is the phase travelled (negative: backwards), `perSample`
            the phase per sample (positive), and `timeOffset` how many samples
            before the current one the segment ended. `skipZero` leaves out the
            edge at phase 0, for the stretch after a sync restart. */
        void addEdges (const BlepTable& table, double p, double segment, double perSample, double timeOffset,
                       double pulseWidth, float gain, bool skipZero) noexcept
        {
            switch (kind)
            {
                case Kind::sine:
                    break;

                case Kind::saw:
                    if (! skipZero)
                        addEdge (table, false, p, segment, perSample, timeOffset, 0.0, -2.0, gain);
                    break;

                case Kind::pulse:
                    if (! skipZero)
                        addEdge (table, false, p, segment, perSample, timeOffset, 0.0, 2.0, gain);
                    addEdge (table, false, p, segment, perSample, timeOffset, pulseWidth, -2.0, gain);
                    break;

                case Kind::triangle:
                    // The slope is +-4 per cycle, so each corner changes it by +-8.
                    if (! skipZero)
                        addEdge (table, true, p, segment, perSample, timeOffset, 0.0, 8.0 * perSample, gain);
                    addEdge (table, true, p, segment, perSample, timeOffset, 0.5, -8.0 * perSample, gain);
                    break;
            }
        }

        /** A sync restart: the old cycle's last stretch, the new cycle's first, and the
            jump (and change of slope) between where the old one was and where the new one starts. */
        void addReset (const BlepTable& table, double p, double dt, double pulseWidth, float gain, const Reset& r) noexcept
        {
            addEdges (table, r.oldPhase, (1.0 - r.fraction) * dt, dt, r.fraction, pulseWidth, gain, false);
            addEdges (table, p, r.fraction * dt, dt, 0.0, pulseWidth, gain, true);

            const Wave wave = waveOf();
            const double jump = naiveWave (wave, 0.0, pulseWidth) - naiveWave (wave, r.oldPhase, pulseWidth);
            if (jump != 0.0)
                place (table, false, r.fraction, jump, gain);

            const double slope = (naiveSlope (wave, 0.0) - naiveSlope (wave, r.oldPhase)) * dt;
            if (slope != 0.0)
                place (table, true, r.fraction, slope, gain);
        }

        /** If the waveform passes phase `at` on the way to `p`, adds a step of `amount` there
            (`ramp`: a slope change of `amount` per sample instead). Travelling backwards turns a step round. */
        void addEdge (const BlepTable& table, bool ramp, double p, double segment, double perSample, double timeOffset,
                      double at, double amount, float gain) noexcept
        {
            double sinceEdge = segment >= 0.0 ? p - at : at - p;
            if (sinceEdge < 0.0)
                sinceEdge += 1.0;

            if (sinceEdge >= std::abs (segment))
                return;

            // The edge was `fraction` of a sample before this sample.
            const double fraction = std::min (sinceEdge / perSample + timeOffset, 0.999999);
            place (table, ramp, fraction, ramp || segment >= 0.0 ? amount : -amount, gain);
        }

        void place (const BlepTable& table, bool ramp, double fraction, double amount, float bandLimit) noexcept
        {
            const double scaled = fraction * BlepTable::oversampling;
            const int row = (int) scaled;
            const float blend = (float) (scaled - (double) row);

            const float* a = ramp ? table.rampRow (row) : table.stepRow (row);
            const float* b = ramp ? table.rampRow (row + 1) : table.stepRow (row + 1);
            const float gain = (float) amount * bandLimit;

            // Tap j lands on the sample (j - halfWidth) after this one. The ring is as long as the kernel, so the
            // taps are one run up to the end of the ring and a second from its start: no index mask in the loop.
            const int slot = (int) ((pos + BlepTable::halfWidth) & mask);
            const int untilWrap = BlepTable::width - slot;

            for (int j = 0; j < untilWrap; ++j)
                ring[(size_t) (slot + j)] += gain * (a[j] + (b[j] - a[j]) * blend);

            for (int j = untilWrap; j < BlepTable::width; ++j)
                ring[(size_t) (j - untilWrap)] += gain * (a[j] + (b[j] - a[j]) * blend);
        }

        Kind kind;
        std::array<float, BlepTable::width> ring {};
        size_t pos = 0;
    };
}

/** Band-limited oscillator with a double-precision phase accumulator.

    Saw and pulse use a windowed-sinc BLEP and triangle a matching BLAMP, so
    harmonics that would land above Nyquist are removed instead of aliasing
    back down. The sine has no edges but is delayed the same way.

    Everything runs `latencySamples` late (the BLEP kernel is symmetric), so
    the first `latencySamples` samples after `reset` are silent and then the
    wave starts at its start phase. Waves are aligned with each other, which
    is what lets two of them be crossfaded.

    Three hooks serve the oscillator interactions; all are neutral until used:
      - `setBandLimit`: 1 is the band-limited wave, 0 the naive one.
      - `setPhaseOffset`: phase modulation (FM) from another oscillator.
      - `syncTo`: hard sync, a restart of the cycle inside the next sample. */
class Oscillator
{
public:
    static constexpr int latencySamples = BlepTable::halfWidth;

    void setFrequency (double hz, double sampleRate) noexcept { dt = hz / sampleRate; }

    /** Sets the per-sample phase increment directly (cycles per sample). */
    void setIncrement (double cyclesPerSample) noexcept { dt = cyclesPerSample; }

    void setPulseWidth (double width) noexcept { pulseWidth = width; }

    /** 1 (the default) removes the aliasing; 0 leaves the naive wave; in between is a mix of the two. */
    void setBandLimit (float amount) noexcept { bandLimit = amount; }

    /** Phase modulation in cycles, added to the phase from the next sample on. It runs the wave
        through the phase `offset` ahead without moving the oscillator's own cycle. */
    void setPhaseOffset (double cycles) noexcept { offset = cycles; }

    /** Hard sync: the next sample's cycle began again `fractionOfSample` (0 up to, not including, 1)
        of a sample ago. The restart is corrected like any other edge. A fresh oscillator ignores it. */
    void syncTo (double fractionOfSample) noexcept { syncFraction = fractionOfSample; }

    /** How long ago the cycle wrapped, as a fraction of a sample, if it did between the last sample and
        the one about to be generated; -1 when it did not. This is what a synced oscillator listens for. */
    double wrapFraction() const noexcept { return ! fresh && dt > 0.0 && phase < dt ? phase / dt : -1.0; }

    void reset (double startPhase = 0.0) noexcept
    {
        phase = startPhase;
        fresh = true;
        offset = 0.0;
        lastOffset = 0.0;
        syncFraction = -1.0;
        active.fill (false);
        for (auto& s : shapes)
            s.clear();
    }

    /** Moves to `newPhase` as if the oscillator had been running there all along: the shapes are
        primed from the phase history when they next run, so there is no gap. */
    void jumpTo (double newPhase) noexcept
    {
        phase = newPhase - fastFloor (newPhase);
        fresh = false;
        active.fill (false);
    }

    double getPhase() const noexcept { return phase; }

    float process (Wave wave) noexcept
    {
        begin();
        used.fill (false);
        const float y = run (wave);
        finishSample();
        return y;
    }

    /** `a` and `b` evaluated at the same phase and mixed: `mix` 0 is all `a`,
        1 is all `b`. */
    float process (Wave a, Wave b, float mix) noexcept
    {
        begin();
        used.fill (false);
        const float ya = run (a);
        const float yb = run (b);
        finishSample();
        return ya + (yb - ya) * mix;
    }

private:
    static constexpr int numShapes = 4;

    /** Settles the phase for this sample: a pending sync restart, and the phase advance modulation makes. */
    void begin() noexcept
    {
        advance = dt + (offset - lastOffset);
        hasReset = false;
        suppressEdges = false;

        if (syncFraction >= 0.0 && ! fresh)
        {
            const double f = std::min (syncFraction, 0.999999);

            if (offset == 0.0 && lastOffset == 0.0 && dt > 0.0)
            {
                double oldPhase = phase - f * dt;
                oldPhase -= fastFloor (oldPhase);
                syncReset = { f, oldPhase };
                hasReset = true;
            }
            else
            {
                suppressEdges = true; // modulated and synced at once: restart without a correction
            }

            phase = f * dt;
        }

        syncFraction = -1.0;
    }

    float run (Wave wave) noexcept
    {
        const size_t index = wave == Wave::triangle ? 0 : wave == Wave::saw ? 1 : wave == Wave::pulse ? 2 : 3;
        auto& shape = shapes[index];

        if (! active[index])
        {
            // Joining mid-note needs history; at the start of a note the
            // shape starts empty and the output is silent until it fills.
            if (! fresh)
                shape.prime (phase, dt, pulseWidth, bandLimit);

            active[index] = true;
        }

        used[index] = true;

        double p = phase;
        if (offset != 0.0)
        {
            p += offset;
            p -= fastFloor (p);
        }

        return shape.tick (p, advance, pulseWidth, ! fresh && ! suppressEdges, bandLimit, hasReset ? &syncReset : nullptr);
    }

    void finishSample() noexcept
    {
        for (size_t i = 0; i < (size_t) numShapes; ++i)
            if (! used[i])
                active[i] = false;

        fresh = false;
        lastOffset = offset;
        phase += dt;
        if (phase >= 1.0)
            phase -= fastFloor (phase);
    }

    using Shape = detail::BandLimitedShape;

    std::array<Shape, numShapes> shapes {
        Shape (Shape::Kind::triangle), Shape (Shape::Kind::saw), Shape (Shape::Kind::pulse), Shape (Shape::Kind::sine)
    };
    std::array<bool, numShapes> active {};
    std::array<bool, numShapes> used {};

    double phase = 0.0;
    double dt = 0.0;
    double pulseWidth = 0.5;
    double offset = 0.0;
    double lastOffset = 0.0;
    double advance = 0.0;         // phase moved since the previous sample, modulation included
    double syncFraction = -1.0;
    Shape::Reset syncReset {};
    float bandLimit = 1.0f;
    bool hasReset = false;
    bool suppressEdges = false;
    bool fresh = true;
};

} // namespace scratchpad::dsp
