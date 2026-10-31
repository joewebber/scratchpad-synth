#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace scratchpad::dsp
{

/** The three morphable wavetables, built once from real harmonic recipes (up to 64 partials), not summed per sample.

    Each table has `numFrames` frames from "soft" (morph 0) to "hard" (morph 1); a read interpolates between two
    neighbouring frames. Every frame is stored at `numMips` band-limits (64, 32, 16 ... 1 harmonics): a read picks the
    richest one whose highest partial stays under Nyquist for the oscillator's increment, so high notes do not alias.

      0  Sweep    1/h^2 sliding to 1/h^0.8: from a soft, nearly triangular tone to a bright buzzy saw.
      1  Vowel    1/h with a formant peak that travels up the spectrum from the 2nd to the 14th harmonic.
      2  Hollow   odd harmonics only at first; the even ones, and a phase twist, come in as it morphs: clarinet to reedy bell.

    `get()` builds the bank on first use (about 20 ms); the voice calls it from `prepare` so the audio thread never does. */
class WavetableBank
{
public:
    static constexpr int numTables = 3;
    static constexpr int numFrames = 8;
    static constexpr int numMips = 7;
    static constexpr int size = 2048;
    static constexpr int maxHarmonics = 64;

    static const WavetableBank& get()
    {
        static const WavetableBank bank;
        return bank;
    }

    /** One sample of table `table` at `phase` (0 .. 1), morph (0 .. 1), for an oscillator advancing `increment` cycles per sample. */
    float read (int table, double phase, float morph, double increment) const noexcept
    {
        int mip = 0;
        if (increment > 0.0)
        {
            const double limit = 0.5 / increment;
            while (mip < numMips - 1 && (double) (maxHarmonics >> mip) > limit)
                ++mip;
        }

        const float position = std::clamp (morph, 0.0f, 1.0f) * (float) (numFrames - 1);
        const int frame = std::min ((int) position, numFrames - 2);
        const float t = position - (float) frame;

        const double x = phase * (double) size;
        const int i0 = (int) x & (size - 1);
        const int i1 = (i0 + 1) & (size - 1);
        const float f = (float) (x - std::floor (x));

        const float* a = frameData (table, frame, mip);
        const float* b = a + (size_t) numMips * (size_t) size; // the next frame, same mip

        const float ya = a[i0] + (a[i1] - a[i0]) * f;
        const float yb = b[i0] + (b[i1] - b[i0]) * f;
        return ya + (yb - ya) * t;
    }

private:
    WavetableBank()
    {
        data.assign ((size_t) numTables * numFrames * numMips * size, 0.0f);

        std::vector<double> sine ((size_t) size), cosine ((size_t) size);
        for (int i = 0; i < size; ++i)
        {
            sine[(size_t) i] = std::sin (2.0 * std::numbers::pi * i / size);
            cosine[(size_t) i] = std::cos (2.0 * std::numbers::pi * i / size);
        }

        for (int table = 0; table < numTables; ++table)
            for (int frame = 0; frame < numFrames; ++frame)
            {
                const double m = (double) frame / (double) (numFrames - 1);
                double scale = 1.0;

                for (int mip = 0; mip < numMips; ++mip)
                {
                    float* out = frameData (table, frame, mip);
                    const int harmonics = maxHarmonics >> mip;

                    for (int h = 1; h <= harmonics; ++h)
                    {
                        const double amp = amplitude (table, h, m);
                        const double phi = phase (table, h, m);
                        const double ac = amp * std::cos (phi), as = amp * std::sin (phi);

                        for (int i = 0; i < size; ++i)
                        {
                            const size_t k = (size_t) ((h * i) & (size - 1));
                            out[i] += (float) (ac * sine[k] + as * cosine[k]);
                        }
                    }

                    if (mip == 0)
                    {
                        float peak = 1.0e-6f;
                        for (int i = 0; i < size; ++i)
                            peak = std::max (peak, std::abs (out[i]));
                        scale = 0.9 / (double) peak; // the same gain for every band-limit of this frame
                    }

                    for (int i = 0; i < size; ++i)
                        out[i] *= (float) scale;
                }
            }
    }

    static double amplitude (int table, int h, double m)
    {
        const double n = (double) h;

        switch (table)
        {
            case 0: return std::pow (n, -(2.0 - 1.2 * m));
            case 1:
            {
                const double centre = 2.0 + 12.0 * m, width = 1.2 + 1.5 * m, d = (n - centre) / width;
                return (1.0 / n) * (1.0 + 7.0 * std::exp (-d * d));
            }
            default: return ((h % 2 == 1) ? 1.0 : 0.9 * m) / std::pow (n, 1.0 - 0.4 * m);
        }
    }

    static double phase (int table, int h, double m)
    {
        return table == 2 ? m * 0.9 * std::numbers::pi * std::sin ((double) h * 1.7) : 0.0;
    }

    // Layout: table, frame, mip, sample. The next frame of the same mip is `numMips * size` further on.
    float* frameData (int table, int frame, int mip) noexcept
    {
        return data.data() + (((size_t) table * numFrames + (size_t) frame) * numMips + (size_t) mip) * size;
    }

    const float* frameData (int table, int frame, int mip) const noexcept
    {
        return data.data() + (((size_t) table * numFrames + (size_t) frame) * numMips + (size_t) mip) * size;
    }

    std::vector<float> data;
};

} // namespace scratchpad::dsp
