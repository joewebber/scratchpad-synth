#pragma once

namespace scratchpad::dsp
{

/** Exact float equality for "did this parameter change?" caching, spelled so
    -Wfloat-equal stays quiet where the intent is deliberate. */
inline bool same (float a, float b) noexcept
{
    return ! (a < b) && ! (a > b);
}

} // namespace scratchpad::dsp
