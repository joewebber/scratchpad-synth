#include "ui/ModuleCards.h"

#include "plugin/Parameters.h"
#include "ui/Neumorph.h"
#include "ui/Typography.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace scratchpad::ui
{

namespace
{
    // Glyphs from the prototype, 12 x 10, for the family dot in a module's header.
    constexpr const char* filterGlyph = "M0.5 3h5.5c1.5 0 2-1 2.5 0s1.5 6 3 6.5";
    constexpr const char* ampGlyph = "M0.5 9.5L3 1.5l2 3h3.5l3 5";

    constexpr int headerHeight = layout::header::height;
    constexpr int bodyGap = 7; // between the header, the picture and the knobs of a card
}

//==============================================================================
FilterCard::FilterCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : curve (p),
      mode (p.apvts, params::filterMode, "Filter mode", p.undoManager, &Palette::flt),
      cutoff (p.apvts, params::filterCutoff, "Cutoff", p.undoManager, "Filter cutoff", layout::knobs::cutoff),
      resonance (p.apvts, params::filterResonance, "Res", p.undoManager, "Filter resonance", layout::knobs::standard),
      keyTrack (p.apvts, params::filterKeyTrack, "Key", p.undoManager, "Filter key tracking", layout::knobs::standard),
      drive (p.apvts, params::filterDrive, "Drive", p.undoManager, "Filter drive", layout::knobs::standard),
      shape ({ "Off", "Tanh", "Hard clip", "Wavefold" }, params::voiceShaperEnabled, "Per-voice shaper", p.undoManager, &Palette::flt),
      shaperDrive (p.apvts, params::voiceShaperDrive, "Shape", p.undoManager, "Per-voice shaper drive", layout::knobs::standard),
      shaperBias (p.apvts, params::voiceShaperBias, "Bias", p.undoManager, "Per-voice shaper bias", layout::knobs::standard),
      shaperMix (p.apvts, params::voiceShaperMix, "Mix", p.undoManager, "Per-voice shaper mix", layout::knobs::standard),
      shaperEnabled (*p.apvts.getParameter (params::voiceShaperEnabled)),
      shaperMode (*p.apvts.getParameter (params::voiceShaperMode)),
      shaperEnabledWatcher (shaperEnabled, [this] (float) { followShaper(); }),
      shaperModeWatcher (shaperMode, [this] (float) { followShaper(); })
{
    setTitle ("Filter");
    makeControllable (p, "filter", false);

    // The shaper's on/off and its shape are two parameters, shown as one row of segments with "Off" first. While the
    // card is full they are on the card, not here.
    makeMore ("Filter", &Palette::flt,
              [this, &p] (MorePopover& popover)
              {
                  if (full)
                      return;

                  popover.addOffChoice (p, params::voiceShaperEnabled, params::voiceShaperMode, "Shape", "Per-voice shaper", 4,
                                        { "Off", "Tanh", "Hard clip", "Wavefold" });
                  popover.addKnobs (p, "Shaper",
                                    { { params::voiceShaperDrive, "Drive", "Per-voice shaper drive", {} },
                                      { params::voiceShaperBias, "Bias", "Per-voice shaper bias", {} },
                                      { params::voiceShaperMix, "Mix", "Per-voice shaper mix", {} } });
              },
              "Remove filter", "Remove Filter", [&p] { p.removeModule (plugin::Module::filter); });

    mode.setDisplayLabels ({ "LP24", "LP12", "HP12", "BP12", "Notch", "Comb" });

    // One pick is one undo step: Off turns the shaper off, a shape turns it on with that shape.
    shape.setColumns (2);
    shape.onPick = [&p] (int i)
    {
        auto& enabled = *p.apvts.getParameter (params::voiceShaperEnabled);
        auto& shapeMode = *p.apvts.getParameter (params::voiceShaperMode);

        p.undoManager.beginNewTransaction (juce::String ("Per-voice shaper ")
                                           + (i == 0 ? juce::String ("off") : shapeMode.getText (shapeMode.convertTo0to1 ((float) (i - 1)), 0)));

        const auto set = [] (juce::RangedAudioParameter& param, float value)
        {
            param.beginChangeGesture();
            param.setValueNotifyingHost (param.convertTo0to1 (value));
            param.endChangeGesture();
        };

        set (enabled, i > 0 ? 1.0f : 0.0f);

        if (i > 0)
            set (shapeMode, (float) (i - 1));
    };

    // The filter envelope's own amount is a connection too, so it shows as an arc on the cutoff.
    cutoff.setModulation (model, { engine::ModDest::cutoff }, p.apvts.getParameter (params::filterEnvAmount));
    resonance.setModulation (model, { engine::ModDest::resonance });

    for (auto* c : std::initializer_list<juce::Component*> { &curve, &mode, &cutoff, &resonance, &keyTrack, &drive })
        addAndMakeVisible (c);

    // The shaper's controls are on the card only in the full layout (`resized` shows them).
    for (auto* c : std::initializer_list<juce::Component*> { &shape, &shaperDrive, &shaperBias, &shaperMix })
        addChildComponent (c);

    applyColours();
    followShaper();
    setFocusOrder ({ &mode, &cutoff, &resonance, &keyTrack, &drive, &shape, &shaperDrive, &shaperBias, &shaperMix, &curve });
}

void FilterCard::followShaper()
{
    const bool on = shaperEnabled.convertFrom0to1 (shaperEnabled.getValue()) > 0.5f;

    shape.setSelectedIndex (on ? 1 + (int) std::lround (shaperMode.convertFrom0to1 (shaperMode.getValue())) : 0);
}

void FilterCard::applyColours()
{
    const auto ink = paletteOf (*this).fltInk;

    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive, &shaperDrive, &shaperBias, &shaperMix })
        k->setFamilyColour (ink);
}

void FilterCard::lookAndFeelChanged()
{
    applyColours();
}

void FilterCard::resized()
{
    constexpr int margin = layout::padding::filter, modeHeight = 24, shaperGap = 8, knobPitch = 6;
    const int innerWidth = getWidth() - 2 * margin;

    // The box picks the layout: the shaper is on the card (and gone from More) from the full size up.
    const bool wasFull = full;
    full = getWidth() >= layout::full::filterWidth && getHeight() >= layout::full::filterHeight;

    if (full != wasFull && isMoreOpen())
        toggleMore(); // its rows were chosen for the other layout

    for (auto* c : std::initializer_list<juce::Component*> { &shape, &shaperDrive, &shaperBias, &shaperMix })
        c->setVisible (full);

    headerRow = { margin, margin, innerWidth, headerHeight };
    placeHeader (headerRow);

    // Bottom up: the shaper row (full only), the Cutoff row, the mode, then the curve takes what is left.
    const int shaperY = getHeight() - margin - shaperDrive.height();
    const int knobsY = (full ? shaperY : getHeight() - margin) - cutoff.height();
    const int modeY = knobsY - bodyGap - modeHeight;
    const int curveY = headerRow.getBottom() + bodyGap;

    curve.setBounds (margin, curveY, innerWidth, juce::jmax (0, modeY - bodyGap - curveY));
    mode.setBounds (margin, modeY, innerWidth, modeHeight);

    int x = margin;

    for (auto* k : { &cutoff, &resonance, &keyTrack, &drive })
    {
        k->setBoundsAt (x, knobsY);
        x += k->getSize().cell + knobPitch;
    }

    if (! full)
        return;

    // Shape, Bias and Mix sit against the right edge; the Shape segments (2 x 2) take the width before them.
    const int cell = layout::knobs::standard.cell;
    int knobX = getWidth() - margin - 3 * cell - 2 * knobPitch;

    shape.setBounds (margin, shaperY, juce::jmax (0, knobX - shaperGap - margin), shaperDrive.height());

    for (auto* k : { &shaperDrive, &shaperBias, &shaperMix })
    {
        k->setBoundsAt (knobX, shaperY);
        knobX += cell + knobPitch;
    }
}

void FilterCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat(), p.flt, filterGlyph, "Filter");
}

//==============================================================================
EnvelopeCard::EnvelopeCard (plugin::ScratchpadProcessor& p, ModulationModel& model, plugin::Module which)
    : isAmp (which == plugin::Module::ampEnv),
      handle (model, which == plugin::Module::ampEnv ? engine::ModSource::ampEnv : engine::ModSource::filterEnv),
      display (p, model, which == plugin::Module::ampEnv),
      attack (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).attack, "A", p.undoManager,
              plugin::ScratchpadProcessor::moduleName (which) + " attack", layout::knobs::envelope),
      decay (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).decay, "D", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + " decay", layout::knobs::envelope),
      sustain (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).sustain, "S", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " sustain", layout::knobs::envelope),
      release (p.apvts, (isAmp ? params::ampEnv : params::filterEnv).release, "R", p.undoManager,
               plugin::ScratchpadProcessor::moduleName (which) + " release", layout::knobs::envelope),
      extra (p.apvts, isAmp ? params::ampEnvVelocity : params::filterEnvAmount, isAmp ? "Vel" : "Amt", p.undoManager,
             plugin::ScratchpadProcessor::moduleName (which) + (isAmp ? " velocity sensitivity" : " amount"), layout::knobs::envelope)
{
    const auto title = juce::String (isAmp ? "Amp Env" : "Filter Env");

    setTitle (title);
    makeControllable (p, isAmp ? "ampEnv" : "filterEnv", false);
    makeMore (title, isAmp ? &Palette::amp : &Palette::flt, {},
              isAmp ? "Remove Amp Envelope (back to the on/off gate)" : "Remove Filter Envelope",
              isAmp ? "Remove Amp Envelope" : "Remove Filter Envelope",
              [&p, which] { p.removeModule (which); });

    extra.setBipolar (! isAmp);

    for (auto* c : std::initializer_list<juce::Component*> { &handle, &display, &attack, &decay, &sustain, &release, &extra })
        addAndMakeVisible (c);

    applyColours();
    setFocusOrder ({ &handle, &attack, &decay, &sustain, &release, &extra, &display });
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
    constexpr int margin = layout::padding::env, compactWellWidth = 50, wellHeight = 44, knobGap = 3;
    constexpr int knobsWidth = 5 * layout::knobs::envelope.cell + 4 * knobGap;
    constexpr int rightInset = layout::filterEnv.w - margin - compactWellWidth - knobGap - knobsWidth; // the compact design's

    headerRow = { margin, margin, getWidth() - 2 * margin, headerHeight };
    placeHeader (headerRow, layout::header::handle);
    handle.setBounds (juce::Rectangle<int> (layout::header::handle, layout::header::handle).withCentre (
        { headerRow.getX() + layout::header::handle / 2, headerRow.getCentreY() }));

    // The knobs keep their size and their distance from the right and bottom edges; the display well takes the extra
    // width and height.
    const int bodyY = headerRow.getBottom() + bodyGap;
    const int growth = juce::jmax (0, getHeight() - layout::filterEnv.h);
    const int knobsY = bodyY + growth;
    const int knobsX = getWidth() - rightInset - knobsWidth;

    display.setBounds (margin, bodyY, juce::jmax (0, knobsX - knobGap - margin), wellHeight + growth);

    int x = knobsX;

    for (auto* k : { &attack, &decay, &sustain, &release, &extra })
    {
        k->setBoundsAt (x, knobsY);
        x += k->getSize().cell + knobGap;
    }
}

void EnvelopeCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat(), isAmp ? p.amp : p.flt, nullptr, getTitle(), layout::header::handle);
}

//==============================================================================
VoiceActivity::VoiceActivity (plugin::ScratchpadProcessor& p)
    : processor (p),
      vblank (this, [this] { tick(); })
{
    setPaintingIsUnclipped (true);
    setTitle ("Voice activity");
}

int VoiceActivity::voiceCount() const
{
    return juce::jlimit (1, (int) shown.size(), (int) std::lround (processor.apvts.getRawParameterValue (params::voicingVoices)->load()));
}

juce::String VoiceActivity::summary() const
{
    return juce::String (sounding) + " of " + juce::String (voiceCount()) + (voiceCount() == 1 ? " voice sounding" : " voices sounding");
}

std::unique_ptr<juce::AccessibilityHandler> VoiceActivity::createAccessibilityHandler()
{
    return a11y::makeImageHandler (*this, [this] { return summary(); });
}

void VoiceActivity::tick()
{
    const bool reduced = motion::isReduced();

    if (! throttle.due (juce::Time::getMillisecondCounterHiRes(), reduced))
        return;

    const auto& live = processor.getLiveModulation();
    bool changed = false;
    int count = 0;

    for (size_t i = 0; i < shown.size(); ++i)
    {
        const float level = live.voiceLevel[i].load (std::memory_order_relaxed);
        const float next = motion::fall (shown[i], level, 0.04f, reduced);

        count += level > 0.0f ? 1 : 0;
        changed = changed || std::abs (next - shown[i]) > 0.002f;
        shown[i] = next;
    }

    changed = changed || count != sounding;
    sounding = count;

    if (changed)
        repaint();
}

void VoiceActivity::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat();

    drawInset (g, bounds, 9.0f, p, p.well);

    constexpr float sideInset = 8.0f, top = 12.0f, readoutSpace = 22.0f;
    const int count = voiceCount();
    const float pitch = (bounds.getWidth() - 2.0f * sideInset) / (float) count;
    const float width = juce::jmin (14.0f, pitch - 4.0f);
    const float bottom = bounds.getHeight() - readoutSpace;

    juce::Path ghost;
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    for (int i = 0; i < count; ++i)
    {
        const float x = sideInset + ((float) i + 0.5f) * pitch;
        ghost.startNewSubPath (x, bottom);
        ghost.lineTo (x, top);
    }

    g.setColour (p.track);
    g.strokePath (ghost, stroke);

    juce::Path active;

    for (int i = 0; i < count; ++i)
    {
        if (shown[(size_t) i] <= 0.0f)
            continue;

        const float x = sideInset + ((float) i + 0.5f) * pitch;
        active.startNewSubPath (x, bottom);
        active.lineTo (x, bottom - (bottom - top) * juce::jlimit (0.0f, 1.0f, shown[(size_t) i]));
    }

    g.setColour (p.ampInk);
    g.strokePath (active, stroke);

    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    g.drawText (summary(), bounds.withTrimmedLeft (8.0f).withTrimmedBottom (5.0f).removeFromBottom (13.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
AmpCard::AmpCard (plugin::ScratchpadProcessor& p, ModulationModel& model)
    : voices (p),
      level (model, engine::ModDest::ampLevel, "Level", "Amp level", 100.0f,
             [] (float percent) { return juce::jlimit (0.0f, 1.0f, percent / 100.0f); }, "100%"),
      pan (model, engine::ModDest::pan, "Pan", "Amp pan", 0.0f,
           [] (float percent) { return juce::jlimit (0.0f, 1.0f, (percent + 100.0f) / 200.0f); }, "Centre")
{
    setTitle ("Amp");
    makeControllable (p, "amp", false, false);

    addAndMakeVisible (voices);
    addAndMakeVisible (level);
    addAndMakeVisible (pan);
    setFocusOrder ({});
}

void AmpCard::resized()
{
    constexpr int margin = layout::padding::filter;

    headerRow = { margin, margin, getWidth() - 2 * margin, headerHeight };
    placeHeader (headerRow);

    const int knobsY = getHeight() - margin - level.height();
    const int voicesY = headerRow.getBottom() + bodyGap;

    voices.setBounds (margin, voicesY, getWidth() - 2 * margin, knobsY - bodyGap - voicesY);
    level.setBoundsAt (margin, knobsY);
    pan.setBoundsAt (margin + layout::knobs::standard.cell + 6, knobsY);
}

void AmpCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    drawHeader (g, p, headerRow.toFloat(), p.amp, ampGlyph, "Amp");
}

//==============================================================================
OutputCard::OutputCard (plugin::ScratchpadProcessor& p)
    : volume (p.apvts, params::outVolume, "Volume", p.undoManager, "Output volume", layout::knobs::out),
      limiter ([&p] { return p.getLimiterReductionDb(); }),
      meter ([&p] { return p.takeOutputPeak(); }, Meter::Orientation::vertical)
{
    setTitle ("Output");
    setExplainKey ("output");
    meter.getProperties().set ("explainId", "control.meter");

    addAndMakeVisible (volume);
    addAndMakeVisible (limiter);
    addAndMakeVisible (meter);

    volume.setFamilyColour (paletteOf (*this).ampInk);
    setFocusOrder ({ &volume });
}

void OutputCard::lookAndFeelChanged()
{
    volume.setFamilyColour (paletteOf (*this).ampInk);
}

void OutputCard::resized()
{
    constexpr int marginX = 9, marginY = 7, limiterWidth = 44, meterWidth = 16, meterHeight = 40, knobGap = 10;
    const int bodyY = marginY + headerHeight + 5;
    const int extra = juce::jmax (0, getHeight() - layout::out.h); // the meter takes the extra height; the rest keeps its place

    limiter.setBounds (getWidth() - marginX - limiterWidth, marginY, limiterWidth, headerHeight);
    volume.setBoundsAt (marginX, bodyY);
    meter.setBounds (marginX + layout::knobs::out.cell + knobGap, bodyY, meterWidth, meterHeight + extra);
}

void OutputCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    g.setColour (p.ink);
    g.setFont (uiFont (layout::header::nameSize, Weight::medium));
    g.drawText ("Out", juce::Rectangle<int> (9, 7, 44, headerHeight), juce::Justification::centredLeft, false);
}

} // namespace scratchpad::ui
