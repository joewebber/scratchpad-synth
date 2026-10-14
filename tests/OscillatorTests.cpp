#include "TestUtils.h"
#include "dsp/Oscillator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using scratchpad::dsp::Oscillator;
using scratchpad::dsp::Wave;

namespace
{
    constexpr double sampleRate = 48000.0;

    std::vector<float> render (Wave wave, double hz, size_t numSamples, double sr = sampleRate)
    {
        Oscillator osc;
        osc.setFrequency (hz, sr);
        std::vector<float> out (numSamples);
        for (auto& s : out)
            s = osc.process (wave);
        return out;
    }

    /** Frequency from the spacing of rising zero crossings, with linear
        interpolation between samples. Skips the first few hundred samples,
        where the note is still starting from silence. */
    double measureHz (const std::vector<float>& x, double sr)
    {
        std::vector<double> crossings;

        for (size_t i = 256; i < x.size(); ++i)
        {
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
                crossings.push_back ((double) (i - 1) + (double) -x[i - 1] / (double) (x[i] - x[i - 1]));
        }

        REQUIRE (crossings.size() > 10);
        const double cycles = (double) (crossings.size() - 1);
        return cycles * sr / (crossings.back() - crossings.front());
    }
}

TEST_CASE ("Oscillator pitch is within 0.1 cent across the range", "[oscillator]")
{
    const auto wave = GENERATE (Wave::sine, Wave::triangle, Wave::saw, Wave::pulse);

    // Sine goes right up the keyboard; the stepped waves stop where a
    // zero-crossing measurement still has enough samples per cycle.
    const int highestNote = wave == Wave::sine ? 120 : 96;

    for (int note = 0; note <= highestNote; note += 3)
    {
        const double hz = testutil::midiToHz (note);
        const auto numSamples = (size_t) (std::clamp (200.0 / hz, 2.0, 30.0) * sampleRate);
        const double measured = measureHz (render (wave, hz, numSamples), sampleRate);

        INFO ("wave " << (int) wave << " note " << note << " expected " << hz << " measured " << measured);
        REQUIRE (std::abs (testutil::centsBetween (measured, hz)) < 0.1);
    }
}

TEST_CASE ("Band-limited waves keep aliasing 60 dB below the fundamental up to C7 at 48 kHz", "[oscillator][aliasing]")
{
    const auto wave = GENERATE (Wave::triangle, Wave::saw, Wave::pulse);

    constexpr size_t n = 16384;

    for (int note = 48; note <= 96; note += 4) // C7 is note 96
    {
        const double hz = testutil::midiToHz (note);
        const auto spectrum = testutil::hannSpectrum (render (wave, hz, n));

        const double binHz = sampleRate / (double) n;
        const int fundamentalBin = (int) std::lround (hz / binHz);

        double fundamental = 0.0;
        for (int b = fundamentalBin - 3; b <= fundamentalBin + 3; ++b)
            fundamental = std::max (fundamental, spectrum[(size_t) b]);

        // Everything not near a true harmonic is aliasing. "Near" is wide enough
        // to clear the Hann window's own sidelobes (about -75 dB at 16 bins).
        double worstAlias = 0.0;
        for (size_t b = 16; b < spectrum.size(); ++b)
        {
            bool nearHarmonic = false;
            for (int h = 1; (double) h * hz < sampleRate * 0.5 + 2.0 * binHz; ++h)
            {
                if (std::abs ((double) b * binHz - (double) h * hz) < 16.0 * binHz)
                {
                    nearHarmonic = true;
                    break;
                }
            }

            if (! nearHarmonic)
                worstAlias = std::max (worstAlias, spectrum[b]);
        }

        const double db = 20.0 * std::log10 (worstAlias / fundamental + 1.0e-12);
        INFO ("wave " << (int) wave << " note " << note << " alias " << db << " dB");
        REQUIRE (db < -60.0);
    }
}

TEST_CASE ("Waves stay inside the expected amplitude range", "[oscillator]")
{
    const auto wave = GENERATE (Wave::sine, Wave::triangle, Wave::saw, Wave::pulse);

    for (double hz : { 55.0, 440.0, 3520.0 })
    {
        for (float s : render (wave, hz, 4800))
        {
            REQUIRE (std::abs (s) <= 1.2f); // band-limiting ripple overshoots by under 20%
        }
    }
}

TEST_CASE ("A note is silent for the oscillator latency, then starts at phase zero", "[oscillator]")
{
    constexpr int latency = Oscillator::latencySamples;

    for (auto wave : { Wave::sine, Wave::triangle, Wave::saw, Wave::pulse })
    {
        const auto x = render (wave, 440.0, 400);

        for (int i = 0; i < latency; ++i)
            REQUIRE (std::abs (x[(size_t) i]) < 1.0e-6f);

        // Phase 0 is the start of the cycle: 0 for a sine, the bottom of the
        // saw and triangle, the top of the pulse.
        const float expected = wave == Wave::sine ? 0.0f : wave == Wave::pulse ? 1.0f : -1.0f;
        INFO ("wave " << (int) wave);
        REQUIRE (std::abs (x[(size_t) latency + 1] - expected) < 0.2f);
    }
}
