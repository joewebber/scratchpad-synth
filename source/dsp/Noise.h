#pragma once

#include <cstdint>

namespace scratchpad::dsp
{

/** White noise (xorshift32) or pink noise (Paul Kellet's refined filter),
    one instance per voice. White is uniform in [-1, 1); pink is scaled to the
    same RMS so switching colour does not change the loudness. */
class Noise
{
public:
    void seed (std::uint32_t s) noexcept
    {
        state = s != 0 ? s : 0x9e3779b9u;
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0f;
    }

    void setPink (bool shouldBePink) noexcept { pink = shouldBePink; }

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;

        const float white = (float) (std::int32_t) state * (1.0f / 2147483648.0f);

        if (! pink)
            return white;

        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;

        return out * pinkGain;
    }

private:
    // Makes pink's RMS match white's (1 / sqrt 3); checked by the noise test.
    static constexpr float pinkGain = 0.328f;

    std::uint32_t state = 0x9e3779b9u;
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    bool pink = false;
};

} // namespace scratchpad::dsp
