#include "ui/EffectCards.h"

#include "plugin/Parameters.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace scratchpad::ui
{

namespace
{
    constexpr int badgeSize = 16;

    // The name starts after the grip (8 px), the badge and a 4 px gap each; `placeHeader` adds the header gap itself.
    constexpr int headerHandleWidth = 8 + 4 + badgeSize + 4 - layout::header::gap;

    float rawValue (plugin::ScratchpadProcessor& p, const char* id)
    {
        return p.apvts.getRawParameterValue (id)->load();
    }
}

//==============================================================================
EffectDisplay::EffectDisplay (plugin::ScratchpadProcessor& p, engine::EffectKind k)
    : processor (p), kind (k)
{
    setTitle (plugin::ScratchpadProcessor::moduleName (effectModule (k)) + " picture");
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, false);
}

void EffectDisplay::visibilityChanged()
{
    if (isShowing())
        startTimerHz (20);
    else
        stopTimer();
}

EffectDisplay::Values EffectDisplay::read() const
{
    Values v {};

    switch (kind)
    {
        case engine::EffectKind::distortion:
            v[0] = rawValue (processor, params::distortionDrive);
            v[1] = rawValue (processor, params::distortionTone);
            v[2] = rawValue (processor, params::distortionMix);
            v[3] = rawValue (processor, params::distortionMode);
            break;

        case engine::EffectKind::chorus:
            v[0] = rawValue (processor, params::chorusRate);
            v[1] = rawValue (processor, params::chorusDepth);
            v[2] = rawValue (processor, params::chorusMix);
            break;

        case engine::EffectKind::delay:
            v[0] = rawValue (processor, params::delayTime);
            v[1] = rawValue (processor, params::delaySync);
            v[2] = rawValue (processor, params::delayDivision);
            v[3] = rawValue (processor, params::delayFeedback);
            v[4] = rawValue (processor, params::delayPingPong);
            v[5] = rawValue (processor, params::delayMix);
            v[6] = (float) processor.getBpm();
            break;

        case engine::EffectKind::reverb:
            v[0] = rawValue (processor, params::reverbSize);
            v[1] = rawValue (processor, params::reverbDecay);
            v[2] = rawValue (processor, params::reverbDamping);
            v[3] = rawValue (processor, params::reverbMix);
            break;
    }

    return v;
}

std::unique_ptr<juce::AccessibilityHandler> EffectDisplay::createAccessibilityHandler()
{
    return a11y::makeImageHandler (*this, [this] { return describe(); });
}

juce::String EffectDisplay::describe() const
{
    const auto text = [this] (const char* id) { return a11y::spokenValue (processor.apvts.getParameter (id)->getCurrentValueAsText()); };

    switch (kind)
    {
        case engine::EffectKind::distortion:
            return "How hard the sound is squashed: drive " + text (params::distortionDrive) + ", tone " + text (params::distortionTone)
                   + ", mix " + text (params::distortionMix) + ".";

        case engine::EffectKind::chorus:
            return "A slowly swaying delay: rate " + text (params::chorusRate) + ", depth " + text (params::chorusDepth)
                   + ", mix " + text (params::chorusMix) + ".";

        case engine::EffectKind::delay:
        {
            const bool synced = rawValue (processor, params::delaySync) > 0.5f;
            return juce::String ("Echoes that repeat and fade: ") + (synced ? "one every " + text (params::delayDivision) + " note" : "time " + text (params::delayTime))
                   + ", feedback " + text (params::delayFeedback) + ", mix " + text (params::delayMix)
                   + ", ping-pong " + text (params::delayPingPong).toLowerCase() + ".";
        }

        case engine::EffectKind::reverb:
            return "A fading tail: size " + text (params::reverbSize) + ", decay " + text (params::reverbDecay)
                   + ", damping " + text (params::reverbDamping) + ", mix " + text (params::reverbMix) + ".";
    }

    return {};
}

void EffectDisplay::timerCallback()
{
    const auto now = read();

    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

void EffectDisplay::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    drawInset (g, getLocalBounds().toFloat(), 8.0f, p, p.well);

    const auto area = getLocalBounds().toFloat().reduced (6.0f, 5.0f);
    const auto values = read();

    switch (kind)
    {
        case engine::EffectKind::distortion: paintDistortion (g, area, values); break;
        case engine::EffectKind::chorus:     paintChorus (g, area, values); break;
        case engine::EffectKind::delay:      paintDelay (g, area, values); break;
        case engine::EffectKind::reverb:     paintReverb (g, area, values); break;
    }
}

// Input against output for a sine of full scale: the harder the drive, the flatter the top.
void EffectDisplay::paintDistortion (juce::Graphics& g, juce::Rectangle<float> area, const Values& v) const
{
    const auto& p = paletteOf (*this);
    const float gain = std::pow (10.0f, v[0] / 20.0f);
    const float mix = v[2] * 0.01f;
    const float norm = std::tanh (gain);
    const float dashes[] = { 2.0f, 4.0f };

    g.setColour (p.track);
    g.drawDashedLine ({ area.getX(), area.getCentreY(), area.getRight(), area.getCentreY() }, dashes, 2, 1.0f);
    g.drawDashedLine ({ area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom() }, dashes, 2, 1.0f);

    // Untouched: a straight diagonal.
    g.setColour (p.mute.withAlpha (0.5f));
    g.drawDashedLine ({ area.getX(), area.getBottom(), area.getRight(), area.getY() }, dashes, 2, 1.0f);

    juce::Path curve;
    constexpr int points = 48;

    for (int i = 0; i <= points; ++i)
    {
        const float in = -1.0f + 2.0f * (float) i / (float) points;
        const float wet = std::tanh (gain * in) / norm;
        const float out = in + mix * (wet - in);
        const float x = area.getX() + (in + 1.0f) * 0.5f * area.getWidth();
        const float y = area.getCentreY() - out * 0.5f * area.getHeight();

        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    g.setColour (p.fxInk);
    g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    static const char* const modeWords[] = { "Tanh", "Hard clip", "Wavefold" };
    g.setColour (p.ink2);
    g.setFont (uiFont (11.0f));
    g.drawText (modeWords[juce::jlimit (0, 2, (int) std::lround (v[3]))], area.expanded (2.0f, 3.0f), juce::Justification::bottomRight, false);
}

// The delay time a chorus sweeps, over one and a half seconds: faster and deeper as the knobs say.
void EffectDisplay::paintChorus (juce::Graphics& g, juce::Rectangle<float> area, const Values& v) const
{
    const auto& p = paletteOf (*this);
    const float cycles = v[0] * 1.5f;
    const float amplitude = 0.2f + 0.8f * v[1] * 0.01f;
    const float mix = v[2] * 0.01f;
    const float dashes[] = { 2.0f, 4.0f };

    g.setColour (p.track);
    g.drawDashedLine ({ area.getX(), area.getCentreY(), area.getRight(), area.getCentreY() }, dashes, 2, 1.0f);

    juce::Path wave;
    constexpr int points = 90;

    for (int i = 0; i <= points; ++i)
    {
        const float t = (float) i / (float) points;
        const float x = area.getX() + t * area.getWidth();
        const float y = area.getCentreY() - std::sin (juce::MathConstants<float>::twoPi * cycles * t) * amplitude * 0.45f * area.getHeight();

        if (i == 0)
            wave.startNewSubPath (x, y);
        else
            wave.lineTo (x, y);
    }

    g.setColour (p.fxInk.withAlpha (0.35f + 0.65f * mix));
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Each echo is a bar: the note, then its repeats, spaced by the delay time and fading by the feedback.
void EffectDisplay::paintDelay (juce::Graphics& g, juce::Rectangle<float> area, const Values& v) const
{
    const auto& p = paletteOf (*this);
    const bool synced = v[1] > 0.5f;
    const float ms = synced ? params::delaySyncMs (v[6], (int) std::lround (v[2])) : v[0];
    const float feedback = v[3] * 0.01f;
    const bool pingPong = v[4] > 0.5f;
    const float mix = v[5] * 0.01f;

    constexpr float windowMs = 2000.0f, barWidth = 3.0f;
    const float baseline = pingPong ? area.getCentreY() : area.getBottom();
    const float room = pingPong ? area.getHeight() * 0.5f : area.getHeight();
    const float span = area.getWidth() - barWidth;

    const auto bar = [&] (float x, float level, bool downwards)
    {
        const float h = juce::jmax (2.0f, level * room);
        const float y = downwards ? baseline : baseline - h;
        g.fillRoundedRectangle (x, y, barWidth, h, 1.0f);
    };

    g.setColour (p.ink2);
    bar (area.getX(), 1.0f, false);
    if (pingPong)
        bar (area.getX(), 1.0f, true);

    g.setColour (p.fxInk);
    for (int k = 1; k <= 32; ++k)
    {
        const float t = (float) k * ms;
        const float level = mix * std::pow (feedback, (float) (k - 1));

        if (t > windowMs || level < 0.02f)
            break;

        bar (area.getX() + t / windowMs * span, level, pingPong && (k % 2 == 1));
    }

    const auto tag = [&] (const juce::String& text)
    {
        g.setColour (p.ink2);
        g.setFont (uiFont (11.0f));
        g.drawText (text, area.expanded (2.0f, 3.0f), juce::Justification::bottomRight, false);
    };

    tag (synced ? processor.apvts.getParameter (params::delayDivision)->getCurrentValueAsText() : params::timeText (ms / 1000.0f));
}

// Level in dB against time over ten seconds: a straight fall to -60 dB at the decay time, steeper at the
// top end when damping is up. A bigger room starts its tail a little later.
void EffectDisplay::paintReverb (juce::Graphics& g, juce::Rectangle<float> area, const Values& v) const
{
    const auto& p = paletteOf (*this);
    const float size = v[0] * 0.01f;
    const float decay = juce::jmax (0.05f, v[1]);
    const float damping = v[2] * 0.01f;
    const float mix = v[3] * 0.01f;

    constexpr float windowSeconds = 10.0f;
    const float startX = area.getX() + size * area.getWidth() * 0.25f;
    const float top = area.getBottom() - (0.35f + 0.65f * mix) * area.getHeight();

    const auto tail = [&] (float seconds)
    {
        const float endX = startX + juce::jmin (1.0f, seconds / windowSeconds) * (area.getRight() - startX);
        juce::Path path;
        path.startNewSubPath (startX, top);
        path.lineTo (endX, area.getBottom());
        return std::pair<juce::Path, float> { path, endX };
    };

    const auto bright = tail (decay * (1.0f - 0.8f * damping));
    g.setColour (p.fx.withAlpha (0.45f));
    g.strokePath (bright.first, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto body = tail (decay);
    juce::Path fill;
    fill.startNewSubPath (startX, area.getBottom());
    fill.lineTo (startX, top);
    fill.lineTo (body.second, area.getBottom());
    fill.closeSubPath();
    g.setColour (p.fx.withAlpha (0.25f));
    g.fillPath (fill);

    g.setColour (p.fxInk);
    g.strokePath (body.first, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The start of the tail is a short vertical edge: the sound going in.
    g.drawLine (startX, top, startX, area.getBottom(), 1.0f);
}

//==============================================================================
EffectGrip::EffectGrip (const juce::String& effectName)
    : juce::Button ("Reorder " + effectName)
{
    setTitle ("Reorder " + effectName);
    setDescription ("Drag sideways, or press the left and right arrow keys, to change where this effect sits in the chain");
    getProperties().set ("explainId", "control.effectGrip");
    setPaintingIsUnclipped (true);
    setClickingTogglesState (false);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void EffectGrip::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto& p = paletteOf (*this);
    const auto bounds = getLocalBounds().toFloat();

    if (highlighted)
    {
        g.setColour (p.ink.withAlpha (0.06f));
        g.fillRoundedRectangle (bounds, 5.0f);
    }

    // Two columns of three dots, as in the design's 6 x 12 grip.
    g.setColour (highlighted || hasKeyboardFocus (false) ? p.ink : p.mute);
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse (juce::Rectangle<float> (2.2f, 2.2f).withCentre ({ bounds.getCentreX() + (column == 0 ? -1.5f : 1.5f),
                                                                             bounds.getCentreY() + ((float) row - 1.0f) * 4.0f }));

    if (hasKeyboardFocus (false))
        ScratchpadLookAndFeel::drawFocusRing (g, bounds, 5.0f, p.ink);
}

void EffectGrip::mouseDown (const juce::MouseEvent& e)
{
    if (onDragStart)
        onDragStart (e);
}

void EffectGrip::mouseDrag (const juce::MouseEvent& e)
{
    if (onDragMove)
        onDragMove (e);
}

void EffectGrip::mouseUp (const juce::MouseEvent& e)
{
    if (onDragEnd)
        onDragEnd (e);
}

bool EffectGrip::keyPressed (const juce::KeyPress& key)
{
    int step = 0;

    if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::upKey)
        step = -1;
    else if (key.getKeyCode() == juce::KeyPress::rightKey || key.getKeyCode() == juce::KeyPress::downKey)
        step = 1;

    if (step != 0 && onNudge)
    {
        onNudge (step);
        return true;
    }

    return juce::Button::keyPressed (key);
}

//==============================================================================
EffectCard::EffectCard (plugin::ScratchpadProcessor& p, engine::EffectKind effectKind)
    : processor (p),
      kind (effectKind),
      name (plugin::ScratchpadProcessor::moduleName (effectModule (effectKind))),
      grip (name),
      display (p, effectKind)
{
    const auto module = effectModule (kind);
    setTitle (name);
    makeControllable (p, params::effectKeys[(size_t) kind], false);
    makeMore (name, &Palette::fx, [this] (MorePopover& popover) { fillMore (popover); },
              "Remove " + name.toLowerCase(), "Remove " + name, [&p, module] { p.removeModule (module); });

    switch (kind)
    {
        case engine::EffectKind::distortion:
            addKnob (params::distortionDrive, "Drive", "drive");
            addKnob (params::distortionTone, "Tone", "tone");
            addKnob (params::distortionMix, "Mix", "mix");
            break;

        case engine::EffectKind::chorus:
            addKnob (params::chorusRate, "Rate", "rate");
            addKnob (params::chorusDepth, "Depth", "depth");
            addKnob (params::chorusMix, "Mix", "mix");
            break;

        case engine::EffectKind::delay:
            addKnob (params::delayTime, "Time", "time");
            addKnob (params::delayFeedback, "Fdbk", "feedback");
            addKnob (params::delayMix, "Mix", "mix");

            // While synced, the note length takes the time knob's place.
            alternateKnob = addKnob (params::delayDivision, "Time", "note length", false);

            syncWatcher = std::make_unique<juce::ParameterAttachment> (*p.apvts.getParameter (params::delaySync),
                                                                       [this] (float v) { showDelayMode (v > 0.5f); });
            syncWatcher->sendInitialUpdate();
            break;

        case engine::EffectKind::reverb:
            addKnob (params::reverbSize, "Size", "size");
            addKnob (params::reverbDecay, "Decay", "decay");
            addKnob (params::reverbMix, "Mix", "mix");
            break;
    }

    addAndMakeVisible (grip);
    addAndMakeVisible (display);
    applyColours();

    // Tab order: the grip, then the knobs left to right (the delay's note length takes its time knob's place), then
    // the picture. A newly added effect offers its first knob.
    std::vector<juce::Component*> order { &grip };
    for (auto* knob : row)
    {
        order.push_back (knob);

        if (knob == row.front() && alternateKnob != nullptr)
            order.push_back (alternateKnob);
    }
    order.push_back (&display);
    setFocusOrder (order, row.front());
}

Knob* EffectCard::addKnob (const char* id, const char* caption, const juce::String& accessibleSuffix, bool inRow)
{
    ownedKnobs.push_back (std::make_unique<Knob> (processor.apvts, id, caption, processor.undoManager,
                                                  name + " " + accessibleSuffix, layout::knobs::effect));
    auto* knob = ownedKnobs.back().get();
    addAndMakeVisible (*knob);

    if (inRow)
        row.push_back (knob);

    return knob;
}

void EffectCard::fillMore (MorePopover& popover) const
{
    switch (kind)
    {
        case engine::EffectKind::distortion:
            popover.addSegments (processor, params::distortionMode, "Mode", name + " mode", 3, { "Tanh", "Hard clip", "Wavefold" });
            popover.addKnobs (processor, "Shape", { { params::distortionBias, "Bias", name + " bias", [] (Knob& k) { k.setBipolar (true); } } });
            break;

        case engine::EffectKind::delay:
            popover.addSwitch (processor, params::delaySync, "Sync to tempo", name + " tempo sync");
            popover.addSwitch (processor, params::delayPingPong, "Ping-pong", name + " ping-pong", "Bounces the echoes between left and right.");
            break;

        case engine::EffectKind::reverb:
            popover.addKnobs (processor, "Tone", { { params::reverbDamping, "Damping", name + " damping", {} } });
            break;

        case engine::EffectKind::chorus:
            break;
    }
}

void EffectCard::setPosition (int newPosition, int count)
{
    grip.setDescription ("Position " + juce::String (newPosition) + " of " + juce::String (count) + " in the chain. Press the left or right arrow key, or drag sideways, to move this effect earlier or later.");

    if (newPosition != position)
    {
        position = newPosition;
        repaint (headerRow);
    }
}

void EffectCard::showDelayMode (bool synced)
{
    row.front()->setVisible (! synced);
    alternateKnob->setVisible (synced);
}

void EffectCard::applyColours()
{
    const auto ink = paletteOf (*this).fxInk;

    for (auto& k : ownedKnobs)
        k->setFamilyColour (ink);
}

void EffectCard::lookAndFeelChanged()
{
    applyColours();
}

void EffectCard::resized()
{
    namespace pad = layout::padding;
    constexpr int wellWidth = 56, wellHeight = 40, bodyGap = 4, rowGap = 5, gripWidth = 10;

    headerRow = { pad::fx, pad::fx, getWidth() - 2 * pad::fx, layout::header::height };
    grip.setBounds (headerRow.withWidth (gripWidth));

    placeHeader (headerRow, headerHandleWidth);

    const int bodyY = headerRow.getBottom() + rowGap;
    display.setBounds (pad::fx, bodyY, wellWidth, wellHeight);

    int x = pad::fx + wellWidth + bodyGap;
    for (auto* k : row)
    {
        k->setBoundsAt (x, bodyY);
        x += layout::knobs::effect.cell + bodyGap;
    }

    if (alternateKnob != nullptr)
        alternateKnob->setBoundsAt (row.front()->getX(), bodyY);
}

void EffectCard::paint (juce::Graphics& g)
{
    ModuleCard::paint (g);

    const auto& p = paletteOf (*this);
    const auto header = headerRow.toFloat();
    drawHeader (g, p, header, p.fx, nullptr, name, headerHandleWidth);

    if (position > 0)
    {
        const auto badge = juce::Rectangle<float> ((float) badgeSize, (float) badgeSize)
                               .withCentre ({ header.getX() + 8.0f + 4.0f + (float) badgeSize * 0.5f, header.getCentreY() });
        g.setColour (p.fx);
        g.fillEllipse (badge);
        g.setColour (p.onAccent);
        g.setFont (uiFont (11.0f));
        g.drawText (juce::String (position), badge, juce::Justification::centred, false);
    }
}

} // namespace scratchpad::ui
