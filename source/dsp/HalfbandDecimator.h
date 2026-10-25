#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace scratchpad::dsp
{

/** Halves the sample rate: a 33-tap linear-phase half-band low-pass (Kaiser window) that is only
    evaluated for the samples that are kept. Every second tap is zero, so an output costs nine
    multiplications.

    The group delay is `delaySamples` input samples (`delaySamples / 2` output samples). The
    oversampled oscillators run `Oscillator::latencySamples` input samples late as well, so a pair
    run at twice the rate and decimated here arrives with the same total delay as one run at the
    base rate: switching between them does not move the sound in time. */
class HalfbandDecimator
{
public:
    static constexpr int delaySamples = 16;

    HalfbandDecimator() noexcept
    {
        constexpr double beta = 7.0;
        std::array<double, centre + 1> h {};

        for (int k = 0; k <= centre; ++k)
        {
            const double r = (double) k / (double) centre;
            const double window = bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta);
            const double sinc = k == 0 ? 0.5 : (k % 2 == 0 ? 0.0 : std::sin (std::numbers::pi * 0.5 * (double) k) / (std::numbers::pi * (double) k));
            h[(size_t) k] = sinc * window;
        }

        double sum = h[0];
        for (int k = 1; k <= centre; ++k)
            sum += 2.0 * h[(size_t) k];

        centreTap = (float) (h[0] / sum);
        for (int m = 0; m < pairs; ++m)
            tap[(size_t) m] = (float) (h[(size_t) (2 * m + 1)] / sum);
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        pos = 0;
    }

    /** Takes two consecutive input samples, `first` then `second`; returns the output sample that
        goes with `first`, which is the one that falls on a base-rate sample (`second` is half a
        base-rate sample later). */
    float process (float first, float second) noexcept
    {
        push (first);

        // `at (0)` is the newest sample. The centre of the filter sits `centre` samples back.
        float y = centreTap * at (centre);
        for (int m = 0; m < pairs; ++m)
        {
            const int d = 2 * m + 1;
            y += tap[(size_t) m] * (at (centre - d) + at (centre + d));
        }

        push (second);
        return y;
    }

private:
    static constexpr int centre = delaySamples;
    static constexpr int pairs = centre / 2; // taps at +-1, +-3 ... +-15
    static constexpr size_t size = 64;       // power of two, at least 2 * centre + 1

    void push (float x) noexcept
    {
        pos = (pos + 1) & (size - 1);
        history[pos] = x;
    }

    float at (int back) const noexcept { return history[(pos - (size_t) back) & (size - 1)]; }

    static double bessel0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;
        for (int k = 1; k < 60; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
        }
        return sum;
    }

    std::array<float, size> history {};
    std::array<float, pairs> tap {};
    float centreTap = 0.0f;
    size_t pos = 0;
};

} // namespace scratchpad::dsp
