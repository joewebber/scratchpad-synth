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
Scope::Segments::Segments (juce::StringArray labelsToShow, juce::StringArray spokenLabels, std::function<void (int)> onPick)
    : labels (std::move (labelsToShow)), spoken (std::move (spokenLabels)), pick (std::move (onPick))
{
    setWantsKeyboardFocus (true);
    setPaintingIsUnclipped (true);
}

void Scope::Segments::setSelected (int i, juce::Colour fill)
{
    const bool changed = i != selected;
    selected = i;
    fillColour = fill;
    repaint();

    if (changed)
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
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
            ScratchpadLookAndFeel::drawSelectedEdge (g, seg, 7.0f, p, fillColour, p.well);
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
    if (k.getModifiers().isAnyModifierKeyDown())
        return false;

    const int code = k.getKeyCode();

    if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey) && selected > 0)
        pick (selected - 1);
    else if ((code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey) && selected + 1 < labels.size())
        pick (selected + 1);
    else if (code == juce::KeyPress::homeKey && selected > 0)
        pick (0);
    else if (code == juce::KeyPress::endKey && selected + 1 < labels.size())
        pick (labels.size() - 1);
    else
        return false;

    return true;
}

std::unique_ptr<juce::AccessibilityHandler> Scope::Segments::createAccessibilityHandler()
{
    return a11y::makeSelectorHandler (
        *this, [this] { return spoken[juce::jlimit (0, spoken.size() - 1, selected)]; },
        [this] (const juce::String& text)
        {
            const int found = spoken.indexOf (text.trim(), true);
            if (found >= 0)
                pick (found);
        });
}

//==============================================================================
Scope::Scope (plugin::ScratchpadProcessor& p)
    : processor (p),
      modeButtons ({ "Wave", "Spectrum" }, { "Wave", "Spectrum" }, [this] (int i) { setMode (i == 0 ? Mode::wave : Mode::spectrum); }),
      tapButtons ({ "Osc", "Filter", "Amp", "FX" }, { "Oscillators", "Filter", "Amp", "Effects" }, [this] (int i) { setTap ((ScopeTap) i); })
{
    setTitle ("Scope");
    setFocusContainerType (FocusContainerType::focusContainer);
    modeButtons.setTitle ("Scope display");
    modeButtons.setDescription ("Wave shows the shape of the sound. Spectrum shows how much of each pitch range is in it.");
    tapButtons.setTitle ("Scope tap");
    tapButtons.setDescription ("Where in the signal path the scope listens: after the oscillators, the filter, the amp or the effects.");
    modeButtons.setExplicitFocusOrder (1);
    tapButtons.setExplicitFocusOrder (2);
    scratch.resize ((size_t) engine::scopeFifoSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));

    modeButtons.getProperties().set ("explainId", "control.scopeMode");
    tapButtons.getProperties().set ("explainId", "control.scopeTap");
    addAndMakeVisible (modeButtons);
    addAndMakeVisible (tapButtons);
    setTap (tap);
    setMode (mode);
}

Scope::~Scope()
{
    stopLive();
}

std::unique_ptr<juce::AccessibilityHandler> Scope::createAccessibilityHandler()
{
    return std::make_unique<a11y::DescribedHandler> (*this, juce::AccessibilityRole::group, [this] { return captionText(); });
}

void Scope::visibilityChanged()
{
    if (isVisible())
    {
        processor.readScope (scratch.data(), (int) scratch.size()); // drop what queued up while nobody looked
        startLive (30);
    }
    else
    {
        stopLive();
    }
}

void Scope::lookAndFeelChanged()
{
    // The selected segments carry a palette colour, so a new theme has to hand them the new one.
    modeButtons.setSelected (mode == Mode::wave ? 0 : 1, paletteOf (*this).src);
    tapButtons.setSelected ((int) tap, tapColour (paletteOf (*this)));
}

void Scope::setMode (Mode m)
{
    mode = m;
    modeButtons.setSelected (m == Mode::wave ? 0 : 1, paletteOf (*this).src);
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

void Scope::liveTick()
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
        waveScale = motion::follow (waveScale, target, 0.25f, motion::isReduced());
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
            bars[(size_t) b] = motion::fall (bars[(size_t) b], db, 3.0f, motion::isReduced()); // quick up, slower fall
        }
    }

    repaint();
}

juce::String Scope::captionText() const
{
    static const char* where[] = { "right after the oscillators and noise, before anything shapes them",
                                   "after the filter, before the amp",
                                   "after the amp, before any effects",
                                   "after the effects and the safety limiter: this is what reaches the output" };

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
