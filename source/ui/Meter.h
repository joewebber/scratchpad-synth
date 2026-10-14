#pragma once

#include "ui/LookAndFeel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace scratchpad::ui
{

/** Horizontal output level meter, -60 dBFS to 0, repainted on the display's
    vertical blank. `readPeak` returns the peak since its last call. */
class Meter final : public juce::Component
{
public:
    explicit Meter (std::function<float()> readPeakFn)
        : readPeak (std::move (readPeakFn)),
          vblank (this, [this] { tick(); })
    {
        setTitle ("Output level");
        setInterceptsMouseClicks (false, false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        auto bounds = getLocalBounds().toFloat();
        const float radius = bounds.getHeight() * 0.5f;

        drawInset (g, bounds, radius, p, p.well);

        const float fraction = juce::jlimit (0.0f, 1.0f, (shownDb + 60.0f) / 60.0f);
        if (fraction > 0.0f)
        {
            const auto track = bounds.reduced (2.0f);
            const auto fill = track.withWidth (juce::jmax (track.getHeight(), track.getWidth() * fraction));
            g.setColour (shownDb > -3.0f ? p.fxInk : p.ampInk);
            g.fillRoundedRectangle (fill, track.getHeight() * 0.5f);
        }
    }

private:
    void tick()
    {
        const float peak = readPeak();
        const float db = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -100.0f;

        // Fast attack, slow fall (about 20 dB per second).
        const float fall = shownDb - 20.0f / 60.0f;
        const float next = juce::jmax (db, fall);

        if (std::abs (next - shownDb) > 0.05f)
        {
            shownDb = next;
            repaint();
        }
    }

    std::function<float()> readPeak;
    float shownDb = -100.0f;
    juce::VBlankAttachment vblank;
};

} // namespace scratchpad::ui
