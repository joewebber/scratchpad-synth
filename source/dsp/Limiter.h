#pragma once

#include <algorithm>
#include <cmath>

namespace scratchpad::dsp
{

/** The always-on safety limiter: no look-ahead, so no latency.

    The two channels are linked (one gain for both, from the louder of the pair). A static
    curve maps the level to a target gain: unity up to `kneeStart` (-0.9 dBFS, so everything below
    that, -6 dBFS included, passes untouched), then a quadratic
    soft knee whose slope falls from 1 to 0 exactly at the ceiling, then a flat ceiling. The
    gain follows the target downwards instantly and recovers with a one-pole release, so
    nothing a signal does can get past the ceiling; a final clamp removes the last rounding
    error. A signal that never reaches the knee passes bit-exact (the gain is exactly 1).

    `getReductionDb` is the gain reduction a meter wants: the deepest reduction of each
    block, falling back at about 58 dB per second. */
class Limiter
{
public:
    static constexpr float ceiling = 0.9660509f; // -0.3 dBFS
    static constexpr float kneeStart = 0.9f;     // -0.9 dBFS: below this the limiter does nothing
    static constexpr float kneeWidth = 2.0f * (ceiling - kneeStart);

    void prepare (double sampleRate) noexcept
    {
        releaseCoefficient = (float) std::exp (-1.0 / (releaseSeconds * sampleRate));
        meterDecayPerSample = (float) (1.0 / (meterSeconds * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        gain = 1.0f;
        meterDb = 0.0f;
    }

    /** Gain the limiter wants for a (linked) peak level `level` >= 0. */
    static float targetGain (float level) noexcept
    {
        if (level <= kneeStart)
            return 1.0f;

        const float t = level - kneeStart;

        if (t >= kneeWidth)
            return ceiling / level;

        return (kneeStart + t - t * t / (2.0f * kneeWidth)) / level;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float lowest = gain;

        for (int i = 0; i < numSamples; ++i)
        {
            const float level = std::max (std::abs (left[i]), std::abs (right[i]));
            const float target = targetGain (level);

            if (target < gain)
            {
                gain = target;
            }
            else
            {
                gain = target + (gain - target) * releaseCoefficient;

                if (target >= 1.0f && gain > 0.999999f)
                    gain = 1.0f;
            }

            lowest = std::min (lowest, gain);

            left[i] = std::clamp (left[i] * gain, -ceiling, ceiling);
            right[i] = std::clamp (right[i] * gain, -ceiling, ceiling);
        }

        const float blockDb = lowest >= 1.0f ? 0.0f : -20.0f * std::log10 (lowest);
        const float fallen = meterDb - meterDecayPerSample * 8.685889638f * (float) numSamples; // 8.686 dB per `meterSeconds`

        meterDb = std::max (blockDb, fallen);

        if (meterDb < 0.001f)
            meterDb = 0.0f;
    }

    float getReductionDb() const noexcept { return meterDb; }

private:
    static constexpr double releaseSeconds = 0.1;
    static constexpr double meterSeconds = 0.15;

    float gain = 1.0f;
    float releaseCoefficient = 0.999f;
    float meterDb = 0.0f;
    float meterDecayPerSample = 0.001f;
};

} // namespace scratchpad::dsp
