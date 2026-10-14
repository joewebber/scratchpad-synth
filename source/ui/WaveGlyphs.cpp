#include "ui/WaveGlyphs.h"

#include <cmath>
#include <numbers>

namespace scratchpad::ui
{

using dsp::Wave;

juce::String waveName (Wave w)
{
    switch (w)
    {
        case Wave::sine:     return "Sine";
        case Wave::triangle: return "Triangle";
        case Wave::saw:      return "Saw";
        case Wave::pulse:    return "Pulse";
    }
    return {};
}

juce::String waveTag (Wave w)
{
    switch (w)
    {
        case Wave::sine:     return "Smooth";
        case Wave::triangle: return "Mellow";
        case Wave::saw:      return "Bright";
        case Wave::pulse:    return "Hollow";
    }
    return {};
}

juce::String waveDescription (Wave w)
{
    switch (w)
    {
        case Wave::sine:     return "Pure and round: one frequency and no harmonics at all.";
        case Wave::triangle: return "Soft and hollow: a few odd harmonics that fade away quickly.";
        case Wave::saw:      return "Bright and buzzy: every harmonic is there, which gives a filter lots to remove.";
        case Wave::pulse:    return "Reedy and hollow: odd harmonics only, and you can change its width later.";
    }
    return {};
}

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

std::vector<float> harmonicAmplitudes (Wave w, int count)
{
    std::vector<float> a ((size_t) count, 0.0f);

    for (int n = 1; n <= count; ++n)
    {
        float v = 0.0f;
        switch (w)
        {
            case Wave::sine:     v = n == 1 ? 1.0f : 0.0f; break;
            case Wave::triangle: v = n % 2 ? 1.0f / (float) (n * n) : 0.0f; break;
            case Wave::saw:      v = 1.0f / (float) n; break;
            case Wave::pulse:    v = n % 2 ? 1.0f / (float) n : 0.0f; break;
        }
        a[(size_t) (n - 1)] = v;
    }

    return a;
}

void drawHarmonics (juce::Graphics& g, juce::Rectangle<float> area, Wave w, int count, juce::Colour colour)
{
    const auto amps = harmonicAmplitudes (w, count);
    constexpr float rangeDb = 48.0f, edge = 14.0f, bottomPad = 7.0f, maxHeight = 40.0f, barWidth = 5.0f;
    const float spacing = count > 1 ? (area.getWidth() - 2.0f * edge) / (float) (count - 1) : 0.0f;
    const float baseline = area.getBottom() - bottomPad;

    g.setColour (colour);

    for (int i = 0; i < count; ++i)
    {
        const float a = amps[(size_t) i];

        // Harmonics that are absent still get a dot, so the row reads as a row.
        const float h = a > 0.0f ? juce::jmax (3.0f, (20.0f * std::log10 (a) + rangeDb) / rangeDb * maxHeight)
                                 : 0.0f;
        const float x = area.getX() + edge + spacing * (float) i;
        const float top = baseline - juce::jmax (h, 0.1f);

        juce::Path bar;
        bar.startNewSubPath (x, baseline);
        bar.lineTo (x, top);
        g.strokePath (bar, juce::PathStrokeType (barWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace scratchpad::ui
