#include "ui/ModuleCards.h"

#include "plugin/Parameters.h"
#include "ui/WaveGlyphs.h"

#include <cmath>
#include <utility>

namespace scratchpad::ui
{

using dsp::Wave;

namespace
{
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

    constexpr const char* noiseGlyph = "M0.5 5l1.5-3 1.5 6 1.5-5 1.5 4 1.5-3 1.5 4 1.5-3";
    constexpr const char* filterGlyph = "M0.5 3h5.5c1.5 0 2-1 2.5 0s1.5 6 3 6.5";
    constexpr const char* gateGlyph = "M0.5 9h2.5V1.5h6V9h2.5";
    constexpr const char* envelopeGlyph = "M0.5 9L3.5 1.5l3 3.5h3L11.5 9";
    constexpr const char* outputGlyph = "M1 5h9 M7.5 2l3 3-3 3";

    std::vector<const char*> waveSelectorGlyphs()
    {
        std::vector<const char*> glyphs;
        for (int i = 0; i < dsp::numWaves; ++i)
            glyphs.push_back (selectorGlyph ((Wave) i));
        return glyphs;
    }

    juce::String oscName (int index) { return "Oscillator " + juce::String (index + 1); }

    // The knobs of a card sit on a fixed 52 px pitch.
    constexpr int knobPitch = Knob::knobWidth;
}

//==============================================================================
ModuleCard::ModuleCard()
{
    setPaintingIsUnclipped (true);
}

void ModuleCard::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawRaised (g, getLocalBounds().toFloat(), 12.0f, p, Elevation::small, p.surface);
}

void ModuleCard::makeRemovable (const juce::String& accessibleName, std::function<void()> onRemove)
{
    removeButton = std::make_unique<IconButton> (accessibleName, "M5.5 5.5l5 5 M10.5 5.5l-5 5", 16.0f, 1.3f);
    removeButton->onClick = std::move (onRemove);
    addAndMakeVisible (*removeButton);
}

void ModuleCard::placeRemoveButton (juce::Rectangle<int> row)
{
    if (removeButton != nullptr)
        removeButton->setBounds (row.removeFromRight (24).withSizeKeepingCentre (24, 20));
}

void ModuleCard::drawHeader (juce::Graphics& g, const Palette& p, juce::Rectangle<float> row,
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

//==============================================================================
OscillatorCard::OscillatorCard (plugin::ScratchpadProcessor& p, int oscillatorIndex)
    : index (oscillatorIndex),
      waveSelector (p.apvts, params::osc[(size_t) oscillatorIndex].wave, oscName (oscillatorIndex) + " wave", p.undoManager, &Palette::src),
      phaseReset (p.apvts, params::osc[(size_t) oscillatorIndex].phaseReset, "Phase reset",
                  oscName (oscillatorIndex) + " phase reset", p.undoManager, &Palette::srcInk),
      octave (p.apvts, params::osc[(size_t) oscillatorIndex].octave, "Octave", p.undoManager, oscName (oscillatorIndex) + " octave"),
      semitone (p.apvts, params::osc[(size_t) oscillatorIndex].semitone, "Semi", p.undoManager, oscName (oscillatorIndex) + " semitone"),
      fine (p.apvts, params::osc[(size_t) oscillatorIndex].fine, "Fine", p.undoManager, oscName (oscillatorIndex) + " fine tune"),
      level (p.apvts, params::osc[(size_t) oscillatorIndex].level, "Level", p.undoManager, oscName (oscillatorIndex) + " level"),
      pulseWidth (p.apvts, params::osc[(size_t) oscillatorIndex].pulseWidth, "Pulse", p.undoManager, oscName (oscillatorIndex) + " pulse width"),
      waveWatcher (*p.apvts.getParameter (params::osc[(size_t) oscillatorIndex].wave),
                   [this] (float v) { setWave ((int) std::lround (v)); })
{
    setTitle (oscName (index));

    waveSelector.setGlyphs (waveSelectorGlyphs());
    if (index == 0)
        waveSelector.onPick = [&p] (int w) { p.changeWave (w); };

    if (index > 0)
    {
        const auto which = index == 1 ? plugin::Module::osc2 : plugin::Module::osc3;
        makeRemovable ("Remove " + oscName (index), [&p, which] { p.removeModule (which); });
    }

    for (auto* c : std::initializer_list<juce::Component*> { &waveSelector, &phaseReset, &octave, &semitone, &fine, &level, &pulseWidth })
        addAndMakeVisible (c);

    applyColours();
    waveWatcher.sendInitialUpdate();
}

void OscillatorCard::applyColours()
{
    const auto ink = paletteOf (*this).srcInk;
    for (auto* k : { &octave, &semitone, &fine, &level, &pulseWidth })
        k->setFamilyColour (ink);
}

void OscillatorCard::lookAndFeelChanged()
{
    applyColours();
}

void OscillatorCard::setWave (int newWave)
{
    wave = juce::jlimit (0, dsp::numWaves - 1, newWave);
    pulseWidth.setVisible (wave == (int) Wave::pulse);
    repaint();
}

void OscillatorCard::resized()
{
    constexpr int top = 6, midX = 70, midWidth = 104, knobsX = 180;

    waveWell = { pad, top, 56, height - 2 * top };
    headerRow = { midX, top, midWidth, 20 };
    placeRemoveButton (headerRow);
    waveSelector.setBounds (midX, top + 25, midWidth, 24);
    phaseReset.setBounds (midX, top + 54, midWidth, 20);

    int x = knobsX;
    for (auto* k : { &octave, &semitone, &fine, &level, &pulseWidth })
    {
        k->setBounds (x, top, Knob::knobWidth, Knob::knobHeight);
        x += knobPitch;
    }
}

void OscillatorCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (index > 0 ? 26.0f : 0.0f), p.src, headerGlyph ((Wave) wave),
                "Osc " + juce::String (index + 1));

    drawInset (g, waveWell.toFloat(), 10.0f, p, p.well);
    const auto drawArea = waveWell.toFloat().reduced (6.0f, 0.0f);
    const float dashes[] = { 2.0f, 4.0f };
    g.setColour (p.track);
    g.drawDashedLine ({ drawArea.getX(), drawArea.getCentreY(), drawArea.getRight(), drawArea.getCentreY() }, dashes, 2, 1.0f);
    g.setColour (p.srcInk);
    g.strokePath (wavePath ((Wave) wave, drawArea, 1.5f, 0.6f),
                  juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
}

//==============================================================================
NoiseCard::NoiseCard (plugin::ScratchpadProcessor& p)
    : colour (p.apvts, params::noiseColour, "Noise colour", p.undoManager, &Palette::src),
      level (p.apvts, params::noiseLevel, "Level", p.undoManager, "Noise level"),
      colourWatcher (*p.apvts.getParameter (params::noiseColour),
                     [this] (float v) { colourIndex = (int) std::lround (v); repaint(); })
{
    setTitle ("Noise");
    makeRemovable ("Remove Noise", [&p] { p.removeModule (plugin::Module::noise); });

    addAndMakeVisible (colour);
    addAndMakeVisible (level);
    applyColours();
    colourWatcher.sendInitialUpdate();
}

void NoiseCard::applyColours()
{
    level.setFamilyColour (paletteOf (*this).srcInk);
}

void NoiseCard::lookAndFeelChanged()
{
    applyColours();
}

void NoiseCard::resized()
{
    constexpr int top = 6, midX = pad, blockWidth = 166, knobsX = 180;

    headerRow = { midX, top, blockWidth, 20 };
    placeRemoveButton (headerRow);
    colour.setBounds (midX, top + 25, blockWidth, 24);
    level.setBounds (knobsX, top, Knob::knobWidth, Knob::knobHeight);

    const int textX = knobsX + knobPitch + 12;
    textArea = { textX, top + 4, width - pad - textX, 60 };
}

void NoiseCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (26.0f), p.src, noiseGlyph, "Noise");

    const auto font = uiFont (12.0f);
    g.setFont (font);
    g.setColour (p.mute);
    const char* text = colourIndex == 0 ? "White noise: every frequency equally loud, a bright hiss. Try it with the filter."
                                        : "Pink noise: less treble, more body, like steady rain.";
    drawLines (g, wrapLines (font, text, (float) textArea.getWidth()), textArea.getPosition().toFloat(),
               (float) textArea.getWidth(), 16.0f);
}

//==============================================================================
FilterCard::FilterCard (plugin::ScratchpadProcessor& p)
    : mode (p.apvts, params::filterMode, "Filter mode", p.undoManager, &Palette::flt),
      cutoff (p.apvts, params::filterCutoff, "Cutoff", p.undoManager, "Filter cutoff"),
      resonance (p.apvts, params::filterResonance, "Reso", p.undoManager, "Filter resonance"),
      keyTrack (p.apvts, params::filterKeyTrack, "Key track", p.undoManager, "Filter key tracking"),
      drive (p.apvts, params::filterDrive, "Drive", p.undoManager, "Filter drive")
{
    setTitle ("Filter");
    makeRemovable ("Remove Filter", [&p] { p.removeModule (plugin::Module::filter); });

    for (auto* c : std::initializer_list<juce::Component*> { &mode, &cutoff, &resonance, &keyTrack, &drive })
        addAndMakeVisible (c);

    applyColours();
}

void FilterCard::applyColours()
{
    const auto ink = paletteOf (*this).fltInk;
    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive })
        k->setFamilyColour (ink);
}

void FilterCard::lookAndFeelChanged()
{
    applyColours();
}

void FilterCard::resized()
{
    auto inner = getLocalBounds().reduced (pad, 0);

    headerRow = inner.withTrimmedTop (pad).withHeight (headerHeight);
    placeRemoveButton (headerRow);
    mode.setBounds (inner.getX(), 36, inner.getWidth(), 24);

    constexpr int knobs = 4;
    const int gap = (inner.getWidth() - knobs * Knob::knobWidth) / (knobs - 1);
    int x = inner.getX();
    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive })
    {
        k->setBounds (x, 66, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + gap;
    }
}

void FilterCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (26.0f), p.flt, filterGlyph, "Filter");
}

//==============================================================================
EnvelopeCard::EnvelopeCard (plugin::ScratchpadProcessor& p, plugin::Module which)
    : isAmp (which == plugin::Module::ampEnv),
      attack (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).attack, "Attack", p.undoManager,
              plugin::ScratchpadProcessor::moduleName (which) + " attack"),
      decay (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).decay, "Decay", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + " decay"),
      sustain (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).sustain, "Sustain", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " sustain"),
      release (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).release, "Release", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " release"),
      extra (p.apvts, isAmp ? params::ampEnvVelocity : params::filterEnvAmount, isAmp ? "Velocity" : "Amount", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + (isAmp ? " velocity sensitivity" : " amount"))
{
    setTitle (plugin::ScratchpadProcessor::moduleName (which));
    makeRemovable ("Remove " + plugin::ScratchpadProcessor::moduleName (which), [&p, which] { p.removeModule (which); });

    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
        addAndMakeVisible (k);

    applyColours();
}

void EnvelopeCard::applyColours()
{
    const auto& p = paletteOf (*this);
    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
        k->setFamilyColour (isAmp ? p.ampInk : p.fltInk);
}

void EnvelopeCard::lookAndFeelChanged()
{
    applyColours();
}

void EnvelopeCard::resized()
{
    auto inner = getLocalBounds().reduced (pad, 0);

    headerRow = inner.withTrimmedTop (pad).withHeight (headerHeight);
    placeRemoveButton (headerRow);

    constexpr int knobs = 5;
    const int gap = (inner.getWidth() - knobs * Knob::knobWidth) / (knobs - 1);
    int x = inner.getX();
    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
    {
        k->setBounds (x, 36, Knob::knobWidth, Knob::knobHeight);
        x += Knob::knobWidth + gap;
    }
}

void EnvelopeCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat().withTrimmedRight (26.0f), isAmp ? p.amp : p.flt, envelopeGlyph,
                isAmp ? "Amp Envelope" : "Filter Envelope");
}

//==============================================================================
AmpCard::AmpCard (plugin::ScratchpadProcessor& p)
    : processor (p)
{
    setTitle ("Amp");
}

void AmpCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    const bool shaped = processor.isModulePresent (plugin::Module::ampEnv);

    auto inner = getLocalBounds().toFloat().reduced ((float) pad);
    drawHeader (g, p, inner.removeFromTop ((float) headerHeight), p.amp, shaped ? envelopeGlyph : gateGlyph, "Amp");
    inner.removeFromTop (8.0f);
    drawCaption (g, shaped ? "Shaped by an envelope" : "On / off gate", inner.removeFromTop (13.0f), p.ink2);
    inner.removeFromTop (6.0f);

    g.setColour (p.mute);
    const auto font = uiFont (12.0f);
    g.setFont (font);
    const char* text = shaped ? "The envelope sets how each note starts and ends. Velocity sets how hard keys play."
                              : "A fixed 2 ms fade on every note start and stop, so there are no clicks.";
    drawLines (g, wrapLines (font, text, inner.getWidth()), inner.getPosition(), inner.getWidth(), 16.0f);
}

//==============================================================================
OutputCard::OutputCard (plugin::ScratchpadProcessor& p)
    : volume (p.apvts, params::outVolume, "Volume", p.undoManager, "Output volume")
{
    setTitle ("Output");
    addAndMakeVisible (volume);
    volume.setFamilyColour (paletteOf (*this).perfInk);
}

void OutputCard::lookAndFeelChanged()
{
    volume.setFamilyColour (paletteOf (*this).perfInk);
}

void OutputCard::resized()
{
    volume.setBounds ((getWidth() - Knob::knobWidth) / 2, 36, Knob::knobWidth, Knob::knobHeight);
}

void OutputCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, juce::Rectangle<float> ((float) pad + 2.0f, (float) pad, (float) width - 2.0f * pad, (float) headerHeight),
                p.perf, outputGlyph, "Out");
}

} // namespace scratchpad::ui
