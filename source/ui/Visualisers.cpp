#include "ui/Visualisers.h"

#include "dsp/FilterResponse.h"
#include "dsp/FloatCompare.h"
#include "plugin/Parameters.h"
#include "ui/Neumorph.h"
#include "ui/Typography.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

namespace
{
    constexpr float topDb = 18.0f, bottomDb = -48.0f;

    float raw (plugin::ScratchpadProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }
}

//==============================================================================
FilterCurve::FilterCurve (plugin::ScratchpadProcessor& p)
    : processor (p)
{
    setTitle ("Filter response curve");
    setPaintingIsUnclipped (true);
}

void FilterCurve::visibilityChanged()
{
    if (isShowing())
        startTimerHz (20);
    else
        stopTimer();
}

void FilterCurve::timerCallback()
{
    const float c = raw (processor, params::filterCutoff), r = raw (processor, params::filterResonance), m = raw (processor, params::filterMode);

    if (! dsp::same (c, lastCutoff) || ! dsp::same (r, lastResonance) || ! dsp::same (m, lastMode))
    {
        lastCutoff = c;
        lastResonance = r;
        lastMode = m;
        repaint();
    }
}

float FilterCurve::dbToFraction (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (topDb - db) / (topDb - bottomDb));
}

void FilterCurve::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawInset (g, getLocalBounds().toFloat(), 8.0f, p, p.well);

    const auto area = getLocalBounds().toFloat().reduced (6.0f, 6.0f);
    const auto mode = (dsp::FilterMode) juce::jlimit (0, dsp::numFilterModes - 1, (int) (raw (processor, params::filterMode) + 0.5f));
    const float cutoff = raw (processor, params::filterCutoff);
    const float resonance = raw (processor, params::filterResonance) * 0.01f;
    const double sr = processor.getSampleRate();

    const auto xOf = [&] (float hz) { return area.getX() + area.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yOf = [&] (float db) { return area.getY() + area.getHeight() * dbToFraction (db); };

    // 0 dB reference.
    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (p.track);
    g.drawDashedLine ({ area.getX(), yOf (0.0f), area.getRight(), yOf (0.0f) }, dashes, 2, 1.0f);

    juce::Path line, fill;
    const int points = 120;

    for (int i = 0; i <= points; ++i)
    {
        const float hz = 20.0f * std::pow (1000.0f, (float) i / (float) points);
        const float db = juce::Decibels::gainToDecibels (dsp::filterMagnitude (mode, hz, cutoff, resonance, sr), bottomDb - 6.0f);
        const float x = xOf (hz), y = yOf (db);

        if (i == 0)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, area.getBottom());
            fill.lineTo (x, y);
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }

    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();

    g.setColour (p.flt.withAlpha (0.3f));
    g.fillPath (fill);
    g.setColour (p.fltInk);
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The cutoff.
    const float cx = xOf (cutoff);
    g.setColour (p.fltInk.withAlpha (0.8f));
    g.drawDashedLine ({ cx, area.getY(), cx, area.getBottom() }, dashes, 2, 1.0f);

    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    const auto label = params::frequencyText (cutoff);
    const float tw = textWidth (uiFont (11.0f), label) + 4.0f;
    g.drawText (label, juce::Rectangle<float> (juce::jlimit (area.getX(), area.getRight() - tw, cx + 4.0f), area.getBottom() - 14.0f, tw, 12.0f),
                juce::Justification::centredLeft, false);
}

//==============================================================================
EnvelopeDisplay::EnvelopeDisplay (plugin::ScratchpadProcessor& p, ModulationModel& m, bool isAmpEnvelope)
    : processor (p), model (m), isAmp (isAmpEnvelope)
{
    setTitle (isAmp ? "Amp envelope shape" : "Filter envelope shape");
    setPaintingIsUnclipped (true);
    model.addListener (this);
}

EnvelopeDisplay::~EnvelopeDisplay()
{
    model.removeListener (this);
}

void EnvelopeDisplay::visibilityChanged()
{
    if (isShowing())
        startTimerHz (20);
    else
        stopTimer();
}

EnvelopeDisplay::Shape EnvelopeDisplay::shapeFor (float attack, float decay, float sustain, float release) noexcept
{
    // Times span three decades, so the widths follow a gentle power of the time: a 10 s decay is wider than a
    // 5 ms attack, but the attack is still visible.
    constexpr float hold = 0.16f;
    const float wa = std::pow (std::max (attack, 0.001f), 0.35f), wd = std::pow (std::max (decay, 0.001f), 0.35f),
                wr = std::pow (std::max (release, 0.001f), 0.35f);
    const float scale = (1.0f - hold) / (wa + wd + wr);

    Shape s {};
    s.attackEnd = wa * scale;
    s.decayEnd = s.attackEnd + wd * scale;
    s.holdEnd = s.decayEnd + hold;
    s.releaseEnd = 1.0f;
    s.sustain = juce::jlimit (0.0f, 1.0f, sustain);
    return s;
}

float EnvelopeDisplay::playheadX (const Shape& s, int stage, float level) noexcept
{
    level = juce::jlimit (0.0f, 1.0f, level);

    switch (stage)
    {
        case 1:  return s.attackEnd * level; // attack
        case 2:  return s.attackEnd + (s.decayEnd - s.attackEnd) * (s.sustain < 0.999f ? (1.0f - level) / (1.0f - s.sustain) : 1.0f);
        case 3:  return 0.5f * (s.decayEnd + s.holdEnd);
        case 4:  return s.holdEnd + (s.releaseEnd - s.holdEnd) * (1.0f - std::min (1.0f, level / std::max (s.sustain, 0.05f)));
        default: return -1.0f; // idle: no dot
    }
}

void EnvelopeDisplay::timerCallback()
{
    const auto& ids = isAmp ? params::ampEnv : params::filterEnv;
    const float a = raw (processor, ids.attack), d = raw (processor, ids.decay), s = raw (processor, ids.sustain), r = raw (processor, ids.release);

    if (! dsp::same (a, lastA) || ! dsp::same (d, lastD) || ! dsp::same (s, lastS) || ! dsp::same (r, lastR))
    {
        lastA = a; lastD = d; lastS = s; lastR = r;
        repaint();
    }
}

void EnvelopeDisplay::liveUpdated()
{
    if (! isShowing())
        return;

    const auto& ids = isAmp ? params::ampEnv : params::filterEnv;
    const auto shape = shapeFor (raw (processor, ids.attack), raw (processor, ids.decay), raw (processor, ids.sustain) * 0.01f,
                                 raw (processor, ids.release));
    const float x = playheadX (shape, model.liveEnvStage (isAmp),
                               model.liveSource (isAmp ? engine::ModSource::ampEnv : engine::ModSource::filterEnv));

    if (std::abs (x - dot) > 0.002f)
    {
        dot = x;
        repaint();
    }
}

void EnvelopeDisplay::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawInset (g, getLocalBounds().toFloat(), 8.0f, p, p.well);

    const auto& ids = isAmp ? params::ampEnv : params::filterEnv;
    const auto shape = shapeFor (raw (processor, ids.attack), raw (processor, ids.decay), raw (processor, ids.sustain) * 0.01f,
                                 raw (processor, ids.release));

    const auto area = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
    const auto X = [&] (float f) { return area.getX() + area.getWidth() * f; };
    const auto Y = [&] (float level) { return area.getBottom() - area.getHeight() * level; };

    juce::Path line;
    line.startNewSubPath (X (0.0f), Y (0.0f));
    line.quadraticTo (X (shape.attackEnd * 0.2f), Y (0.85f), X (shape.attackEnd), Y (1.0f)); // fast at first, like the real curve
    line.quadraticTo (X (shape.attackEnd + (shape.decayEnd - shape.attackEnd) * 0.25f), Y (shape.sustain), X (shape.decayEnd), Y (shape.sustain));
    line.lineTo (X (shape.holdEnd), Y (shape.sustain));
    line.quadraticTo (X (shape.holdEnd + (shape.releaseEnd - shape.holdEnd) * 0.2f), Y (0.0f), X (shape.releaseEnd), Y (0.0f));

    juce::Path fill (line);
    fill.lineTo (X (1.0f), Y (0.0f));
    fill.closeSubPath();

    const auto ink = isAmp ? p.ampInk : p.fltInk;
    g.setColour ((isAmp ? p.amp : p.flt).withAlpha (0.3f));
    g.fillPath (fill);
    g.setColour (ink);
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (dot >= 0.0f)
    {
        // x follows the stage, y is the envelope's actual level.
        const float live = model.liveSource (isAmp ? engine::ModSource::ampEnv : engine::ModSource::filterEnv);
        g.setColour (p.ink);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ X (dot), Y (juce::jlimit (0.0f, 1.0f, live)) }));
    }
}

} // namespace scratchpad::ui
