#include "ui/PatchView.h"

#include "plugin/Parameters.h"
#include "ui/WaveGlyphs.h"

namespace scratchpad::ui
{

using dsp::Wave;

namespace
{
    constexpr int waveRadioGroup = 101;
    constexpr int pad = 14;
    constexpr int headerHeight = 22;
    constexpr int oscWidth = 300, oscHeight = 14 + headerHeight + 10 + 96 + 10 + 28 + 14 + 150 + 14;
    constexpr int gateWidth = 170, gateHeight = 150;
    constexpr int outWidth = 170, outHeight = 14 + headerHeight + 10 + 150 + 14;
    constexpr int linkWidth = 40;
    constexpr int knobWidth = 110, knobHeight = 150;
    constexpr int cardRadius = 12;

    // Glyphs from the prototype: 18 x 10 for the wave selector, 12 x 10 for module headers.
    const char* selectorGlyph (Wave w)
    {
        switch (w)
        {
            case Wave::sine:     return "M1 5c2-5.5 5-5.5 8 0s6 5.5 8 0";
            case Wave::triangle: return "M1 5l4-4 8 8 4-4";
            case Wave::saw:      return "M1 1l8 8V1l8 8";
            case Wave::pulse:    return "M1 9V1h8v8h8V1";
        }
        return "";
    }

    const char* headerGlyph (Wave w)
    {
        switch (w)
        {
            case Wave::sine:     return "M0.5 5c1.4-5 3.6-5 5.5 0s4 5 5.5 0";
            case Wave::triangle: return "M0.5 5l2.75-4 5.5 8 2.75-4";
            case Wave::saw:      return "M0.5 1.5L6 8.5V1.5L11.5 8.5";
            case Wave::pulse:    return "M0.5 9V1h5.5v8h5.5V1";
        }
        return "";
    }

    constexpr const char* gateGlyph = "M0.5 9h2.5V1.5h6V9h2.5";
    constexpr const char* outputGlyph = "M1 5h9 M7.5 2l3 3-3 3";

    /** Family-coloured dot with a stroked glyph, then the module name. */
    void drawModuleHeader (juce::Graphics& g, const Palette& p, juce::Rectangle<float> row,
                           juce::Colour family, const char* glyph, const juce::String& title)
    {
        const auto dot = juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ row.getX() + 10.0f, row.getCentreY() });
        g.setColour (family);
        g.fillEllipse (dot);

        auto path = juce::Drawable::parseSVGPath (glyph);
        path.applyTransform (juce::AffineTransform::translation (dot.getX() + 4.0f, dot.getY() + 5.0f));
        g.setColour (p.onAccent);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

        g.setColour (p.ink);
        g.setFont (uiFont (13.0f, Weight::medium));
        g.drawText (title, row.withTrimmedLeft (27.0f), juce::Justification::centredLeft, false);
    }

    void drawChevron (juce::Graphics& g, const Palette& p, juce::Rectangle<int> link)
    {
        const float cx = (float) link.getCentreX(), cy = (float) link.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 2.0f, cy - 4.0f);
        chevron.lineTo (cx + 2.0f, cy);
        chevron.lineTo (cx - 2.0f, cy + 4.0f);
        g.setColour (p.mute);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
/** One segment of the wave selector: a glyph on a flat pill, family-coloured when chosen. */
class PatchView::WaveButton final : public juce::Button
{
public:
    explicit WaveButton (Wave w)
        : juce::Button (waveName (w)), glyph (juce::Drawable::parseSVGPath (selectorGlyph (w)))
    {
        setTitle ("Oscillator 1 wave: " + waveName (w));
        setRadioGroupId (waveRadioGroup);
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        const bool on = getToggleState();

        if (on)
        {
            g.setColour (p.src);
            g.fillRoundedRectangle (bounds, 7.0f);
        }
        else if (highlighted)
        {
            g.setColour (p.ink.withAlpha (0.06f));
            g.fillRoundedRectangle (bounds, 7.0f);
        }

        auto path = glyph;
        path.applyTransform (juce::AffineTransform::translation (bounds.getCentreX() - 9.0f, bounds.getCentreY() - 5.0f));
        g.setColour (on ? p.onAccent : p.ink2);
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, bounds, 7.0f, p.ink);
    }

private:
    juce::Path glyph;
};

//==============================================================================
PatchView::PatchView (plugin::ScratchpadProcessor& p)
    : processor (p),
      levelKnob (p.apvts, params::osc1Level, "Level", params::oscLevelMinDb, p.undoManager),
      volumeKnob (p.apvts, params::outVolume, "Volume", params::volumeMinDb, p.undoManager),
      waveAttachment (*p.apvts.getParameter (params::osc1Wave),
                      [this] (float v) { syncWaveButtons ((int) (v + 0.5f)); },
                      &p.undoManager)
{
    setTitle ("Signal chain");
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);

    for (int i = 0; i < dsp::numWaves; ++i)
    {
        auto& b = waveButtons[(size_t) i];
        b = std::make_unique<WaveButton> ((dsp::Wave) i);
        b->onClick = [this, i]
        {
            if (waveButtons[(size_t) i]->getToggleState() && i != currentWave)
                processor.changeWave (i);
        };
        addAndMakeVisible (*b);
    }

    levelKnob.setFamilyColour (paletteOf (*this).srcInk);
    volumeKnob.setFamilyColour (paletteOf (*this).perfInk);
    addAndMakeVisible (levelKnob);
    addAndMakeVisible (volumeKnob);

    waveAttachment.sendInitialUpdate();
}

PatchView::~PatchView() = default;

void PatchView::syncWaveButtons (int waveIndex)
{
    currentWave = juce::jlimit (0, dsp::numWaves - 1, waveIndex);

    for (int i = 0; i < dsp::numWaves; ++i)
        waveButtons[(size_t) i]->setToggleState (i == currentWave, juce::dontSendNotification);

    repaint (waveWell);
}

void PatchView::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);

    drawRaised (g, getLocalBounds().toFloat(), 18.0f, p, Elevation::raised, p.surface);

    // Title row: "SIGNAL CHAIN" and the one-line "listen for" prompt.
    auto title = uiFont (13.0f, Weight::medium);
    title.setExtraKerningFactor (0.22f);
    g.setFont (title);
    g.setColour (p.ink);
    g.drawText ("SIGNAL CHAIN", juce::Rectangle<float> (14.0f, 12.0f, chainTitleRight - 14.0f, 24.0f),
                juce::Justification::centredLeft, false);

    if (! listenPill.isEmpty())
    {
        const auto pill = listenPill.toFloat();
        g.setColour (p.accentSoft);
        g.fillRoundedRectangle (pill, 12.0f);

        auto text = pill.reduced (10.0f, 0.0f);
        auto captionFont = uiFont (11.0f);
        captionFont.setExtraKerningFactor (0.14f);
        drawCaption (g, "Listen for", text.removeFromLeft (textWidth (captionFont, "LISTEN FOR") + 2.0f), p.ink2);
        text.removeFromLeft (8.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (12.0f));
        g.drawText ("Hold a key. The note slams on, and stops dead when you let go.", text,
                    juce::Justification::centredLeft, true);
    }

    drawChevron (g, p, firstLink);
    drawChevron (g, p, secondLink);

    // Oscillator.
    {
        const auto card = oscCard.toFloat();
        drawRaised (g, card, (float) cardRadius, p, Elevation::small, p.surface);

        drawModuleHeader (g, p, card.reduced ((float) pad, 0.0f).withTrimmedTop ((float) pad).withHeight ((float) headerHeight),
                          p.src, headerGlyph ((Wave) currentWave), "Osc 1");

        drawInset (g, waveWell.toFloat(), 10.0f, p, p.well);
        const auto drawArea = waveWell.toFloat().reduced (8.0f, 0.0f);
        const float dashes[] = { 2.0f, 4.0f };
        g.setColour (p.track);
        g.drawDashedLine ({ drawArea.getX(), drawArea.getCentreY(), drawArea.getRight(), drawArea.getCentreY() }, dashes, 2, 1.0f);
        g.setColour (p.srcInk);
        g.strokePath (wavePath ((dsp::Wave) currentWave, drawArea, 3.0f, 0.68f),
                      juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        drawInset (g, selector.toFloat(), 9.0f, p, p.well);
    }

    // The gate is part of the audio path, so it is on screen.
    {
        const auto card = gateNode.toFloat();
        drawRaised (g, card, (float) cardRadius, p, Elevation::small, p.surface);

        auto inner = card.reduced ((float) pad);
        drawModuleHeader (g, p, inner.removeFromTop ((float) headerHeight), p.amp, gateGlyph, "Amp");
        inner.removeFromTop (12.0f);
        drawCaption (g, "On / off gate", inner.removeFromTop (13.0f), p.ink2);
        inner.removeFromTop (6.0f);
        g.setColour (p.mute);
        const auto font = uiFont (12.0f);
        g.setFont (font);
        drawLines (g, wrapLines (font, "A fixed 2 ms fade on every note start and stop, so there are no clicks.",
                                 inner.getWidth()),
                   inner.getPosition(), inner.getWidth(), 16.0f);
    }

    // Output.
    {
        const auto card = outputCard.toFloat();
        drawRaised (g, card, (float) cardRadius, p, Elevation::small, p.surface);
        drawModuleHeader (g, p, card.reduced ((float) pad, 0.0f).withTrimmedTop ((float) pad).withHeight ((float) headerHeight),
                          p.perf, outputGlyph, "Output");
    }

    g.setColour (p.ink2);
    const auto captionFont = uiFont (14.0f);
    g.setFont (captionFont);
    drawLines (g, wrapLines (captionFont,
                             "One wave, straight to the output. The note is on while a key is down and off when you let go, "
                             "and nothing else is shaping it.", (float) caption.getWidth()),
               { (float) caption.getCentreX(), (float) caption.getY() }, (float) caption.getWidth(), 20.0f, true);
}

void PatchView::resized()
{
    auto title = uiFont (13.0f, Weight::medium);
    title.setExtraKerningFactor (0.22f);
    chainTitleRight = 14.0f + textWidth (title, "SIGNAL CHAIN");
    listenPill = juce::Rectangle<int> ((int) std::ceil (chainTitleRight) + 12, 12,
                                       getWidth() - (int) std::ceil (chainTitleRight) - 12 - 14, 24);

    constexpr int rowWidth = oscWidth + linkWidth + gateWidth + linkWidth + outWidth;
    const int x0 = (getWidth() - rowWidth) / 2;
    const int captionSpace = 28 + 44;
    const int y0 = 36 + (getHeight() - 36 - oscHeight - captionSpace) / 2;
    const int centreY = y0 + oscHeight / 2;

    oscCard = { x0, y0, oscWidth, oscHeight };
    firstLink = { oscCard.getRight(), centreY - 10, linkWidth, 20 };
    gateNode = { firstLink.getRight(), centreY - gateHeight / 2, gateWidth, gateHeight };
    secondLink = { gateNode.getRight(), centreY - 10, linkWidth, 20 };
    outputCard = { secondLink.getRight(), centreY - outHeight / 2, outWidth, outHeight };

    // Oscillator card contents.
    auto inner = oscCard.reduced (pad);
    inner.removeFromTop (headerHeight + 10);
    waveWell = inner.removeFromTop (96);
    inner.removeFromTop (10);

    selector = inner.removeFromTop (28);
    auto segments = selector.reduced (2);
    const int buttonWidth = (segments.getWidth() - 2 * (dsp::numWaves - 1)) / dsp::numWaves;
    for (auto& b : waveButtons)
    {
        b->setBounds (segments.removeFromLeft (buttonWidth));
        segments.removeFromLeft (2);
    }

    inner.removeFromTop (14);
    levelKnob.setBounds (inner.withSizeKeepingCentre (knobWidth, knobHeight));

    // Output card contents.
    auto out = outputCard.reduced (pad);
    out.removeFromTop (headerHeight + 10);
    volumeKnob.setBounds (out.withSizeKeepingCentre (knobWidth, knobHeight));

    caption = { getWidth() / 2 - 340, oscCard.getBottom() + 28, 680, 44 };
}

} // namespace scratchpad::ui
