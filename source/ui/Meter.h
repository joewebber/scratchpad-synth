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
        setInterceptsMouseClicks (true, false);
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


/** The safety limiter's light: lit while the limiter is pulling the level down (more than 0.1 dB), and for a
    short hold afterwards so a brief touch is still seen. Polled on the display's vertical blank like the
    meter. The text "LIM" is always there, so the light is never the only carrier. */
class LimiterLight final : public juce::Component
{
public:
    explicit LimiterLight (std::function<float()> readReductionDbFn)
        : readReductionDb (std::move (readReductionDbFn)),
          vblank (this, [this] { tick(); })
    {
        setTitle ("Safety limiter");
        setDescription ("Idle");
        getProperties().set ("explainId", "control.limiter");
        setInterceptsMouseClicks (true, false);
    }

    bool isLit() const noexcept { return lit; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        const auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ bounds.getX() + 7.0f, bounds.getCentreY() });

        if (lit)
        {
            g.setColour (p.accent.withAlpha (0.25f));
            g.fillEllipse (dot.expanded (3.0f));
            g.setColour (p.accent);
            g.fillEllipse (dot);
        }
        else
        {
            drawInset (g, dot, 5.0f, p, p.well);
        }

        auto font = uiFont (11.0f, Weight::medium);
        font.setExtraKerningFactor (0.1f);
        g.setFont (font);
        g.setColour (lit ? p.ink : p.mute);
        g.drawText ("LIM", bounds.withTrimmedLeft (18.0f), juce::Justification::centredLeft, false);
    }

private:
    void tick()
    {
        // About a quarter of a second at 60 Hz.
        constexpr int holdFrames = 15;

        if (readReductionDb() > 0.1f)
            framesLeft = holdFrames;
        else if (framesLeft > 0)
            --framesLeft;

        const bool shouldLight = framesLeft > 0;

        if (shouldLight != lit)
        {
            lit = shouldLight;
            setDescription (lit ? "Limiting: turning the level down" : "Idle");
            repaint();
        }
    }

    std::function<float()> readReductionDb;
    bool lit = false;
    int framesLeft = 0;
    juce::VBlankAttachment vblank;
};

} // namespace scratchpad::ui
