#include "ui/WaveGlyphs.h"

#include <cmath>
#include <numbers>

namespace scratchpad::ui
{

using dsp::Wave;

static float idealWave (Wave w, float p)
{
    switch (w)
    {
        case Wave::sine:     return std::sin (2.0f * std::numbers::pi_v<float> * p);
        case Wave::triangle: return p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f;
        case Wave::saw:      return 1.0f - 2.0f * p;
        case Wave::pulse:    return p < 0.5f ? 1.0f : -1.0f;
    }
    return 0.0f;
}

juce::Path wavePath (Wave w, juce::Rectangle<float> area, float cycles, float amplitude)
{
    juce::Path path;
    constexpr int steps = 240;
    const float mid = area.getCentreY();
    const float halfHeight = area.getHeight() * 0.5f;

    for (int i = 0; i <= steps; ++i)
    {
        const float x = (float) i / (float) steps;
        float phase = std::fmod (x * cycles, 1.0f);

        // Nudge the last point just inside the cycle so a saw or pulse ends
        // on its high side instead of snapping to the wrap.
        if (i == steps && phase < 1.0e-6f)
            phase = 0.9999f;

        const float y = mid - idealWave (w, phase) * halfHeight * amplitude;
        const float px = area.getX() + x * area.getWidth();

        if (i == 0)
            path.startNewSubPath (px, y);
        else
            path.lineTo (px, y);
    }

    return path;
}

} // namespace scratchpad::ui
