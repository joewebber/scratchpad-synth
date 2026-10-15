#include "ui/Scope.h"

#include "ui/Neumorph.h"
#include "ui/Typography.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

using engine::ScopeTap;

namespace
{
    constexpr float lowestHz = 30.0f, highestHz = 16000.0f;
    constexpr float floorDb = -84.0f;
}

//==============================================================================
Scope::Segments::Segments (juce::StringArray labelsToShow, std::function<void (int)> onPick)
    : labels (std::move (labelsToShow)), pick (std::move (onPick))
{
    setWantsKeyboardFocus (true);
    setPaintingIsUnclipped (true);
}

void Scope::Segments::setSelected (int i, juce::Colour fill)
{
    selected = i;
    fillColour = fill;
    repaint();
}

void Scope::Segments::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat();
    drawInset (g, bounds, 9.0f, p, p.well);

    const float w = (bounds.getWidth() - 4.0f) / (float) labels.size();

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto seg = juce::Rectangle<float> (bounds.getX() + 2.0f + w * (float) i, bounds.getY() + 2.0f, w, bounds.getHeight() - 4.0f);

        if (i == selected)
        {
            g.setColour (fillColour);
            g.fillRoundedRectangle (seg, 7.0f);
        }

        g.setColour (i == selected ? p.onAccent : p.ink2);
        g.setFont (uiFont (12.0f, i == selected ? Weight::medium : Weight::regular));
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds, 9.0f, p.ink);
}

void Scope::Segments::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, (int) ((float) e.x / (float) getWidth() * (float) labels.size()));
    pick (i);
}

bool Scope::Segments::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::leftKey) && selected > 0)  { pick (selected - 1); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey) && selected + 1 < labels.size()) { pick (selected + 1); return true; }
    return false;
}

//==============================================================================
Scope::Scope (plugin::ScratchpadProcessor& p)
    : processor (p),
      modeButtons ({ "Wave", "Spectrum" }, [this] (int i) { setMode (i == 0 ? Mode::wave : Mode::spectrum); }),
      tapButtons ({ "Osc", "Filter", "Amp", "FX" }, [this] (int i) { setTap ((ScopeTap) i); })
{
    setTitle ("Scope");
    modeButtons.setTitle ("Scope display: wave or spectrum");
    tapButtons.setTitle ("Scope tap: where in the signal path to listen");
    scratch.resize ((size_t) engine::scopeFifoSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));

    addAndMakeVisible (modeButtons);
    addAndMakeVisible (tapButtons);
    setTap (tap);
    setMode (mode);
}

Scope::~Scope()
{
    stopTimer();
}

void Scope::visibilityChanged()
{
    if (isVisible())
    {
        processor.readScope (scratch.data(), (int) scratch.size()); // drop what queued up while nobody looked
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void Scope::setMode (Mode m)
{
    mode = m;
    modeButtons.setSelected (m == Mode::wave ? 0 : 1, paletteOf (*this).srcInk);
    repaint();
}

void Scope::setTap (ScopeTap t)
{
    tap = t;
    processor.setScopeTap (t);
    processor.readScope (scratch.data(), (int) scratch.size());
    history.fill (0.0f);
    peaks.fill (floorDb);
    tapButtons.setSelected ((int) t, tapColour (paletteOf (*this)));
    repaint();
}

juce::Colour Scope::tapColour (const Palette& p) const
{
    switch (tap)
    {
        case ScopeTap::oscillators: return p.src;
        case ScopeTap::filter:      return p.flt;
        case ScopeTap::amp:         return p.amp;
        case ScopeTap::effects:     return p.fx;
    }
    return p.src;
}

void Scope::resized()
{
    auto area = getLocalBounds();
    tapButtons.setBounds (area.removeFromTop (24));
    area.removeFromTop (4);
    modeButtons.setBounds (area.removeFromTop (24).removeFromLeft (150));
    area.removeFromTop (6);
    captionArea = area.removeFromBottom (48);
    area.removeFromBottom (4);
    display = area;
}

int Scope::findTrigger (const float* x, int count, int searchFrom, int searchTo)
{
    for (int i = std::max (1, searchFrom); i < std::min (count, searchTo); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            return i;

    return -1;
}

void Scope::timerCallback()
{
    const int got = processor.readScope (scratch.data(), (int) scratch.size());

    for (int i = 0; i < got; ++i)
        history[(size_t) ((written + i) % historySize)] = scratch[(size_t) i];

    written = (written + got) % historySize;

    // The last fftSize samples, oldest first.
    std::array<float, fftSize> latest {};
    for (int i = 0; i < fftSize; ++i)
        latest[(size_t) i] = history[(size_t) ((written - fftSize + i + 2 * historySize) % historySize)];

    float peak = 0.0f;
    for (float s : latest)
        peak = std::max (peak, std::abs (s));

    hasSignal = peak > 1.0e-4f;

    if (mode == Mode::wave)
    {
        // Start where the wave crosses zero going up, so a steady note holds still.
        const int trigger = findTrigger (latest.data(), fftSize, 0, fftSize - (int) shownWave.size());
        const int from = trigger >= 0 ? trigger : fftSize - (int) shownWave.size();

        float shownPeak = 0.0f;
        for (size_t i = 0; i < shownWave.size(); ++i)
        {
            shownWave[i] = latest[(size_t) from + i];
            shownPeak = std::max (shownPeak, std::abs (shownWave[i]));
        }

        // A quiet signal is scaled up so it can be seen; the caption says when it is.
        const float target = 1.0f / std::max (0.25f, shownPeak);
        waveScale += (target - waveScale) * 0.25f;
    }
    else
    {
        for (int i = 0; i < fftSize; ++i)
        {
            fftData[(size_t) i] = latest[(size_t) i] * window[(size_t) i];
            fftData[(size_t) (fftSize + i)] = 0.0f;
        }

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const double sr = processor.getSampleRate();
        const float binHz = (float) sr / (float) fftSize;

        for (int b = 0; b < numBars; ++b)
        {
            const float lo = lowestHz * std::pow (highestHz / lowestHz, (float) b / (float) numBars);
            const float hi = lowestHz * std::pow (highestHz / lowestHz, (float) (b + 1) / (float) numBars);
            const int k0 = std::max (1, (int) std::floor (lo / binHz));
            const int k1 = std::max (k0, std::min (fftSize / 2 - 1, (int) std::ceil (hi / binHz) - 1));

            float m = 0.0f;
            for (int k = k0; k <= k1; ++k)
                m = std::max (m, fftData[(size_t) k]);

            // A full-scale sine reads about 0 dB: the Hann window halves it, hence * 4 / N.
            const float db = juce::Decibels::gainToDecibels (m * 4.0f / (float) fftSize, floorDb);
            bars[(size_t) b] = std::max (db, bars[(size_t) b] - 3.0f); // quick up, slower fall
        }
    }

    repaint();
}

juce::String Scope::captionText() const
{
    static const char* where[] = { "right after the oscillators and noise, before anything shapes them",
                                   "after the filter, before the amp",
                                   "after the amp: this is the level that reaches the output",
                                   "after the effects. There are none yet, so it matches the amp" };

    if (! hasSignal)
        return "Hold a key to see " + juce::String (where[(int) tap]) + ".";

    if (mode == Mode::spectrum)
        return juce::String ("Each bar is a band of frequencies, low on the left, ") + where[(int) tap] + ".";

    return juce::String ("A few cycles of the signal ") + where[(int) tap] + (waveScale > 1.05f ? ". Zoomed in: it is quiet." : ".");
}

void Scope::drawWave (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& p = paletteOf (*this);
    const auto inner = area.reduced (8.0f, 8.0f);

    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (p.track);
    g.drawDashedLine ({ inner.getX(), inner.getCentreY(), inner.getRight(), inner.getCentreY() }, dashes, 2, 1.0f);

    if (! hasSignal)
        return;

    juce::Path path;
    for (size_t i = 0; i < shownWave.size(); ++i)
    {
        const float x = inner.getX() + inner.getWidth() * (float) i / (float) (shownWave.size() - 1);
        const float y = inner.getCentreY() - juce::jlimit (-1.0f, 1.0f, shownWave[i] * waveScale) * inner.getHeight() * 0.5f;

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }

    g.setColour (tapColour (p) == p.src ? p.srcInk : tapColour (p) == p.flt ? p.fltInk : tapColour (p) == p.amp ? p.ampInk : p.fxInk);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Scope::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& p = paletteOf (*this);
    const auto inner = area.reduced (8.0f, 8.0f);
    const float w = inner.getWidth() / (float) numBars;

    g.setColour (tapColour (p) == p.src ? p.srcInk : tapColour (p) == p.flt ? p.fltInk : tapColour (p) == p.amp ? p.ampInk : p.fxInk);

    for (int b = 0; b < numBars; ++b)
    {
        const float level = juce::jlimit (0.0f, 1.0f, (bars[(size_t) b] - floorDb) / -floorDb);
        const float h = std::max (1.0f, level * inner.getHeight());
        g.fillRoundedRectangle (inner.getX() + w * (float) b + 0.5f, inner.getBottom() - h, std::max (1.0f, w - 1.0f), h, 1.0f);
    }

    g.setColour (p.mute);
    g.setFont (uiFont (11.0f));
    g.drawText ("30 Hz", inner.withHeight (12.0f).withTrimmedRight (inner.getWidth() * 0.5f), juce::Justification::topLeft, false);
    g.drawText ("16 kHz", inner.withHeight (12.0f).withTrimmedLeft (inner.getWidth() * 0.5f), juce::Justification::topRight, false);
}

void Scope::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawInset (g, display.toFloat(), 10.0f, p, p.well);

    if (mode == Mode::wave)
        drawWave (g, display.toFloat());
    else
        drawSpectrum (g, display.toFloat());

    g.setColour (p.mute);
    const auto font = uiFont (12.0f);
    g.setFont (font);
    drawLines (g, wrapLines (font, captionText(), (float) captionArea.getWidth()), captionArea.getPosition().toFloat(),
               (float) captionArea.getWidth(), 15.0f);
}

} // namespace scratchpad::ui
