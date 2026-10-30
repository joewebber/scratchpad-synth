#pragma once

namespace scratchpad::dsp
{

/** Reads a circular buffer `delay` samples behind `writeIndex` with Catmull-Rom
    (4-point cubic) interpolation. `writeIndex` is the slot of "now", `buffer[writeIndex - k]`
    being the sample k steps older. `delay` must be at least 1 when "now" has been written
    (chorus) and at least 2 when it has not (delay, reverb: the slot still holds stale data),
    and at most `size - 3`. At a whole-sample delay the result is exactly that sample. */
inline float readCubic (const float* buffer, int size, int writeIndex, float delay) noexcept
{
    const int i = (int) delay;
    const float t = delay - (float) i;

    auto at = [=] (int k) noexcept
    {
        int index = writeIndex - k;
        if (index < 0)
            index += size;
        return buffer[index];
    };

    float p0, p1, p2, p3;

    // The four taps are consecutive slots, running backwards from p0; they only wrap near the start of the buffer.
    const int first = writeIndex - (i - 1);

    if (first >= 3)
    {
        p0 = buffer[first];
        p1 = buffer[first - 1];
        p2 = buffer[first - 2];
        p3 = buffer[first - 3];
    }
    else
    {
        p0 = at (i - 1);
        p1 = at (i);
        p2 = at (i + 1);
        p3 = at (i + 2);
    }

    return p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + t * (3.0f * (p1 - p2) + p3 - p0)));
}

} // namespace scratchpad::dsp
