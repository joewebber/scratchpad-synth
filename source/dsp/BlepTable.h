#pragma once

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** Band-limited step (BLEP) and ramp (BLAMP) residual tables.

    A naive oscillator has a discontinuity (or a corner) wherever its
    waveform jumps; sampling it folds harmonics above Nyquist back down as
    aliasing. Adding the difference between the band-limited and the ideal
    step around each jump removes that aliasing.

    The band-limited step is the integral of a Kaiser-windowed sinc low-pass,
    so the residual is symmetric: it starts `halfWidth` samples before the
    jump and ends `halfWidth` samples after it. The oscillator therefore runs
    `halfWidth` samples late, which is the price of a clean, linear-phase
    result and no pitch or amplitude dependence.

    The tables are laid out as `rows` of `width` taps. Row `i` holds the
    residual for a jump that lands `i / oversampling` of a sample before the
    sample being generated; neighbouring rows are interpolated linearly. */
class BlepTable
{
public:
    static constexpr int halfWidth = 16;           // samples either side of the jump
    static constexpr int width = 2 * halfWidth;    // taps touched per jump; power of two
    static constexpr int oversampling = 256;       // sub-sample resolution (linear interpolation error sets the aliasing floor)
    static constexpr int rows = oversampling + 1;

    static const BlepTable& get()
    {
        static const BlepTable table;
        return table;
    }

    /** Residual for a unit step: `row(i)[j]` for tap j in [0, width). */
    const float* stepRow (int i) const noexcept { return step.data() + (size_t) i * width; }

    /** Residual for a unit change of slope (per sample). */
    const float* rampRow (int i) const noexcept { return ramp.data() + (size_t) i * width; }

private:
    BlepTable()
    {
        // Low-pass cutoff in cycles per sample, and the Kaiser window shape.
        // Chosen so aliasing stays 60 dB under the fundamental up to C7 at
        // 48 kHz while harmonics up to about 19 kHz are kept.
        constexpr double cutoff = 0.43;
        constexpr double beta = 8.0;

        const int n = width * oversampling;
        std::vector<double> kernel ((size_t) n + 1);

        for (int k = 0; k <= n; ++k)
        {
            const double tau = (double) k / oversampling - halfWidth;
            const double x = 2.0 * cutoff * tau;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double r = tau / halfWidth;
            const double window = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            kernel[(size_t) k] = sinc * window;
        }

        // Integrate the kernel into a step (trapezoid rule), normalised to end at 1.
        std::vector<double> blStep ((size_t) n + 1, 0.0);
        for (int k = 1; k <= n; ++k)
            blStep[(size_t) k] = blStep[(size_t) k - 1] + 0.5 * (kernel[(size_t) k - 1] + kernel[(size_t) k]);

        const double norm = blStep[(size_t) n];
        for (auto& v : blStep)
            v /= norm;

        // Integrate the step into a ramp.
        std::vector<double> blRamp ((size_t) n + 1, 0.0);
        for (int k = 1; k <= n; ++k)
            blRamp[(size_t) k] = blRamp[(size_t) k - 1] + 0.5 * (blStep[(size_t) k - 1] + blStep[(size_t) k]) / oversampling;

        // The ideal ramp ends at tau = halfWidth; remove any residual bias so
        // both tails settle to exactly zero.
        const double rampEnd = (double) halfWidth - blRamp[(size_t) n];

        step.resize ((size_t) rows * width);
        ramp.resize ((size_t) rows * width);

        for (int i = 0; i < rows; ++i)
        {
            for (int j = 0; j < width; ++j)
            {
                const int k = j * oversampling + i;
                const double tau = (double) k / oversampling - halfWidth;

                const double idealStep = tau < 0.0 ? 0.0 : (tau > 0.0 ? 1.0 : 0.5);
                const double idealRamp = tau > 0.0 ? tau : 0.0;

                step[(size_t) i * width + (size_t) j] = (float) (blStep[(size_t) k] - idealStep);
                ramp[(size_t) i * width + (size_t) j] = (float) (blRamp[(size_t) k] + rampEnd * (double) k / (double) n - idealRamp);
            }
        }
    }

    /** Modified Bessel function of the first kind, order 0 (series). */
    static double bessel0 (double x)
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

    std::vector<float> step;
    std::vector<float> ramp;
};

} // namespace scratchpad::dsp
