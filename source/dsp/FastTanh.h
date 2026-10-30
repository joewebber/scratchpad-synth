#pragma once

#include <algorithm>

namespace scratchpad::dsp
{

/** `tanh` for audio, as a rational function (the 13th-order odd numerator over a 6th-order even denominator
    used by Eigen's vectorised `tanh`). Within 4e-7 of the true value everywhere (-128 dB for a signal of
    full scale), which is about three float ulps, so it is the same sound as `std::tanh`; but it has no
    library call, no table and no branch, so it costs a fraction of `std::tanh` (which spends most of its
    time in `expm1f`) and a loop of it can be vectorised. Odd, and exactly 0 at 0. `+-inf` give `+-1`,
    NaN gives NaN. Because it is plain float arithmetic, every platform computes the same samples. */
inline float fastTanh (float x) noexcept
{
    // Beyond this the true tanh is within a float epsilon of +-1 and the rational form is too.
    x = std::clamp (x, -7.90531110763549805f, 7.90531110763549805f);
    const float x2 = x * x;

    const float numerator = x * (4.89352455891786e-03f + x2 * (6.37261928875436e-04f + x2 * (1.48572235717979e-05f
                              + x2 * (5.12229709037114e-08f + x2 * (-8.60467152213735e-11f + x2 * (2.00018790482477e-13f
                              + x2 * -2.76076847742355e-16f))))));

    const float denominator = 4.89352518554385e-03f + x2 * (2.26843463243900e-03f + x2 * (1.18534705686654e-04f
                              + x2 * 1.19825839466702e-06f));

    return numerator / denominator;
}

} // namespace scratchpad::dsp
