#pragma once

#include "ui/Accessibility.h"
#include "ui/LookAndFeel.h"
#include "ui/Motion.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace scratchpad::ui
{

/** The level the meter shows after reading `peak` (the peak since the last read): up at once, down about 20 dB a
    second at the display's 60 Hz. Reduced motion shows each reading as it is. */
inline float nextMeterDb (float shownDb, float peak, bool reduced)
{
    const float db = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    return motion::fall (shownDb, db, 20.0f / 60.0f, reduced);
}

/** Whether the limiter's light is on, given each reading of the reduction. It lights while the limiter is pulling
    the level down by more than 0.1 dB and for about a quarter of a second after (15 frames at 60 Hz), so a brief
    touch is still seen. Reduced motion has no hold: the light shows the latest reading, and a reading comes only
    twice a second. */
class LimiterHold
{
public:
    bool step (float reductionDb, bool reduced) noexcept
    {
        constexpr int holdFrames = 15;

        if (reduced)
        {
            framesLeft = 0;
            return reductionDb > 0.1f;
        }

        if (reductionDb > 0.1f)
            framesLeft = holdFrames;
        else if (framesLeft > 0)
            --framesLeft;

        return framesLeft > 0;
    }

private:
    int framesLeft = 0;
};

/** Horizontal output level meter, -60 dBFS to 0, repainted on the display's
    vertical blank (twice a second under reduced motion). `readPeak` returns the peak since its last call.

    For a screen reader it is a read-only level, in decibels, that is read when asked for: it never announces
    itself as it moves. */
class Meter final : public juce::Component
{
public:
    explicit Meter (std::function<float()> readPeakFn)
        : readPeak (std::move (readPeakFn)),
          vblank (this, [this] { tick(); })
    {
        setTitle ("Output level");
        setDescription ("How loud the sound leaving the synth is. 0 dB is as loud as it can go.");
        setInterceptsMouseClicks (true, false);
    }

    /** The level shown, in dB, and in words. */
    float getShownDb() const noexcept { return shownDb; }

    juce::String levelText() const
    {
        return shownDb <= -59.5f ? juce::String ("silent") : juce::String (juce::roundToInt (shownDb)) + " dB";
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
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::progressBar, juce::AccessibilityActions {},
                                                             juce::AccessibilityHandler::Interfaces {
                                                                 std::make_unique<a11y::ReadOnlyRangeValue> (
                                                                     -60.0, 0.0, [this] { return (double) juce::jlimit (-60.0f, 0.0f, shownDb); },
                                                                     [this] { return levelText(); }) });
    }

    void tick()
    {
        const bool reduced = motion::isReduced();

        if (! throttle.due (juce::Time::getMillisecondCounterHiRes(), reduced))
            return;

        const float next = nextMeterDb (shownDb, readPeak(), reduced);

        if (std::abs (next - shownDb) > 0.05f)
        {
            shownDb = next;
            repaint();
        }
    }

    std::function<float()> readPeak;
    float shownDb = -100.0f;
    motion::Throttle throttle;
    juce::VBlankAttachment vblank;
};


/** The safety limiter's light: lit while the limiter is pulling the level down (more than 0.1 dB), and for a
    short hold afterwards so a brief touch is still seen. Polled on the display's vertical blank like the
    meter. The text "LIM" is always there, so the light is never the only carrier.

    A screen reader is told when it comes on ("Safety limiter working"), at most once every ten seconds, and can
    ask for its state at any time. */
class LimiterLight final : public juce::Component
{
public:
    explicit LimiterLight (std::function<float()> readReductionDbFn)
        : readReductionDb (std::move (readReductionDbFn)),
          vblank (this, [this] { tick(); })
    {
        setTitle (idleTitle);
        setDescription ("Turns the level down when it would be too loud, to protect your ears and speakers.");
        getProperties().set ("explainId", "control.limiter");
        setInterceptsMouseClicks (true, false);
    }

    bool isLit() const noexcept { return lit; }

    static inline const juce::String idleTitle { "Safety limiter: idle" }, workingTitle { "Safety limiter: working, turning the level down" };

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
            g.setColour (p.accentInk); // the ring keeps the lit dot visible where the accent is pale against the surface
            g.drawEllipse (dot.reduced (0.5f), 1.0f);
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
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
    }

    void tick()
    {
        const bool reduced = motion::isReduced();

        if (! throttle.due (juce::Time::getMillisecondCounterHiRes(), reduced))
            return;

        const bool shouldLight = hold.step (readReductionDb(), reduced);

        if (shouldLight != lit)
        {
            lit = shouldLight;
            setTitle (lit ? workingTitle : idleTitle);

            if (auto* handler = getAccessibilityHandler())
                handler->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);

            const auto now = juce::Time::getMillisecondCounterHiRes();

            if (lit && now - lastAnnounced > 10000.0)
            {
                lastAnnounced = now;
                a11y::announce ("Safety limiter working");
            }

            repaint();
        }
    }

    std::function<float()> readReductionDb;
    bool lit = false;
    double lastAnnounced = -1.0e9;
    LimiterHold hold;
    motion::Throttle throttle;
    juce::VBlankAttachment vblank;
};

} // namespace scratchpad::ui
