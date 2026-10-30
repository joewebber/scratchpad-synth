#pragma once

#include <cstdint>

namespace scratchpad::dsp
{

/** `std::floor` for a phase accumulator. On a baseline x86-64 build `std::floor` is a call into libm
    (the SSE4.1 rounding instruction is not assumed), which is dear at one call per sample per oscillator.
    This is the same value, bit for bit, for any finite `x` with `|x| < 2^62`, which phases always are (but it gives +0 for
    -0, where `std::floor` keeps the sign). */
inline double fastFloor (double x) noexcept
{
    const double truncated = (double) (std::int64_t) x;
    return truncated > x ? truncated - 1.0 : truncated;
}

} // namespace scratchpad::dsp
