#include "ui/LookAndFeel.h"

#include <cmath>
#include <numbers>

namespace scratchpad::ui
{

namespace
{
}

ScratchpadLookAndFeel::ScratchpadLookAndFeel (const Palette& initial)
    : palette (&initial)
{
    setDefaultSansSerifTypeface (uiFont (12.0f).getTypefacePtr());
    applyColours();
}

void ScratchpadLookAndFeel::setPalette (const Palette& p)
{
    palette = &p;
    applyColours();
}

void ScratchpadLookAndFeel::applyColours()
{
    const auto& p = *palette;

    setColour (juce::ResizableWindow::backgroundColourId, p.bg);
    setColour (juce::TextButton::buttonColourId, p.surface);
    setColour (juce::TextButton::buttonOnColourId, p.src);
    setColour (juce::TextButton::textColourOffId, p.ink2);
    setColour (juce::TextButton::textColourOnId, p.onAccent);

    setColour (juce::Slider::rotarySliderFillColourId, p.srcInk);
    setColour (juce::Slider::rotarySliderOutlineColourId, p.track);
    setColour (juce::Slider::thumbColourId, p.ink);
    setColour (juce::Slider::textBoxTextColourId, p.ink);
    setColour (juce::Slider::textBoxBackgroundColourId, p.well);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, p.accent);

    setColour (juce::Label::textColourId, p.ink);
    setColour (juce::TextEditor::backgroundColourId, p.well);
    setColour (juce::TextEditor::textColourId, p.ink);
    setColour (juce::TextEditor::highlightColourId, p.accent);
    setColour (juce::TextEditor::highlightedTextColourId, p.onAccent);
    setColour (juce::TextEditor::outlineColourId, p.track);
    setColour (juce::TextEditor::focusedOutlineColourId, p.ink);
    setColour (juce::CaretComponent::caretColourId, p.ink);

    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, p.surface);
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, p.keyBlack);
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colours::transparentBlack);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, p.ink.withAlpha (0.06f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, p.accent);
    setColour (juce::MidiKeyboardComponent::textLabelColourId, p.mute);
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, p.surface);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, p.ink);
}

void ScratchpadLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour c)
{
    g.setColour (c);
    g.drawRoundedRectangle (bounds.expanded (2.5f), radius + 2.5f, 2.0f);
}

juce::Font ScratchpadLookAndFeel::getTextButtonFont (juce::TextButton& b, int)
{
    auto font = uiFont ((float) (double) b.getProperties().getWithDefault (buttonProps::fontSize, 12.0));
    font.setExtraKerningFactor ((float) (double) b.getProperties().getWithDefault (buttonProps::tracking, 0.0));
    return font;
}

void ScratchpadLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                                  bool highlighted, bool down)
{
    // The shadow spills past the button's bounds, so buttons paint unclipped.
    const auto& p = *palette;
    const auto bounds = b.getLocalBounds().toFloat();
    const float radius = (float) (double) b.getProperties().getWithDefault (
        buttonProps::radius, (double) juce::jmin (10.0f, bounds.getHeight() * 0.35f));

    const bool on = b.getToggleState();
    juce::Colour fill = on ? b.findColour (juce::TextButton::buttonOnColourId)
                           : b.findColour (juce::TextButton::buttonColourId);

    if (! b.isEnabled())
        fill = fill.interpolatedWith (p.bg, 0.6f);
    else if (down)
        fill = fill.interpolatedWith (on ? p.ink : p.well, on ? 0.12f : 0.5f);
    else if (highlighted)
        fill = fill.interpolatedWith (p.ink, 0.05f);

    drawRaised (g, bounds, radius, p, Elevation::small, fill);

    if (b.hasKeyboardFocus (false))
        drawFocusRing (g, bounds, radius, p.ink);
}

void ScratchpadLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    const auto& p = *palette;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight());

    // Disc, then a track ring 2.5 px outside it, as in the prototype.
    constexpr float stroke = 2.5f, ringGap = 2.5f, margin = 3.0f;
    const float discSize = outer - 2.0f * (margin + ringGap + stroke * 0.5f);
    const auto centre = bounds.getCentre();
    const auto disc = juce::Rectangle<float> (discSize, discSize).withCentre (centre);
    const float ringRadius = discSize * 0.5f + ringGap;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool active = slider.isMouseButtonDown();

    drawRaised (g, disc, discSize * 0.5f, p, active ? Elevation::lifted : Elevation::small, p.surface);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.0f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (active ? 4.0f : stroke, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    const juce::Point<float> inner (centre.x + std::sin (angle) * discSize * 0.12f, centre.y - std::cos (angle) * discSize * 0.12f);
    const juce::Point<float> tip (centre.x + std::sin (angle) * discSize * 0.36f, centre.y - std::cos (angle) * discSize * 0.36f);
    g.setColour (p.ink);
    g.drawLine ({ inner, tip }, 2.2f);

    if (slider.hasKeyboardFocus (false))
        drawFocusRing (g, disc, discSize * 0.5f + ringGap, p.ink);
}

void ScratchpadLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float, float, juce::Slider::SliderStyle style,
                                              juce::Slider& slider)
{
    const auto& p = *palette;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    // Groove with a small raised cap, like the output fader.
    const auto groove = juce::Rectangle<float> (4.0f, bounds.getHeight() - 8.0f).withCentre (bounds.getCentre());
    drawInset (g, groove, 2.0f, p, p.well);

    const auto cap = juce::Rectangle<float> (22.0f, 12.0f).withCentre ({ bounds.getCentreX(), sliderPos });
    drawRaised (g, cap, 4.0f, p, Elevation::small, p.surface);

    g.setColour (p.mute);
    g.fillRoundedRectangle (cap.withSizeKeepingCentre (10.0f, 1.5f), 0.75f);

    if (slider.hasKeyboardFocus (false))
        drawFocusRing (g, cap, 4.0f, p.ink);
}

juce::Label* ScratchpadLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setJustificationType (juce::Justification::centred);
    l->setBorderSize ({ 0, 4, 0, 4 });
    return l;
}

juce::Font ScratchpadLookAndFeel::getLabelFont (juce::Label&)
{
    auto font = uiFont (12.0f);
    return font;
}

void ScratchpadLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto& p = *palette;
    const auto bg = label.findColour (juce::Label::backgroundColourId);

    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
        drawInset (g, label.getLocalBounds().toFloat(), 8.0f, p, p.well);
    else if (bg.getAlpha() > 0)
    {
        g.setColour (bg);
        g.fillRoundedRectangle (label.getLocalBounds().toFloat(), 6.0f);
    }

    if (label.isBeingEdited())
        return;

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    g.setFont (uiFont (12.0f));
    g.drawText (label.getText(), label.getLocalBounds().reduced (4, 0), label.getJustificationType(), false);
}

} // namespace scratchpad::ui
