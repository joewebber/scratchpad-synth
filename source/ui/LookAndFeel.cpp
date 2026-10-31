#include "ui/LookAndFeel.h"

#include "ui/Layout.h"

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

    setColour (juce::PopupMenu::backgroundColourId, p.surface);
    setColour (juce::PopupMenu::textColourId, p.ink);
    setColour (juce::PopupMenu::headerTextColourId, p.mute);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, p.onAccent);

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

void ScratchpadLookAndFeel::drawSelectedEdge (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, const Palette& p,
                                              juce::Colour fill, juce::Colour behind)
{
    if (contrastRatio (fill, behind) >= minGraphicContrast)
        return;

    g.setColour (p.ink2);
    g.drawRoundedRectangle (bounds.reduced (0.6f), radius, 1.2f);
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

void ScratchpadLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path&, juce::Image&)
{
    const auto bounds = box.getLocalBounds().reduced (getCallOutBoxBorderSize (box)).toFloat();
    drawRaised (g, bounds, (float) layout::popover::radius, *palette, Elevation::lifted, palette->surface);
}

juce::Slider::SliderLayout ScratchpadLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (! slider.isRotary())
        return LookAndFeel_V4::getSliderLayout (slider);

    const auto& props = slider.getProperties();
    const int cell = (int) props.getWithDefault (knobMetrics::discSize, knobMetrics::defaultDisc) + 2 * knobMetrics::margin;
    const int valueY = (int) props.getWithDefault (knobMetrics::valueY, cell + knobMetrics::labelGap);

    juce::Slider::SliderLayout layout;
    layout.sliderBounds = juce::Rectangle<int> (cell, cell).withX ((slider.getWidth() - cell) / 2);
    layout.textBoxBounds = { 0, valueY, slider.getWidth(), knobMetrics::labelHeight };
    return layout;
}

void ScratchpadLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    // The design's knob: a disc `size` across with a 2.4 px track 2.5 px outside it, a pointer, and the value arc
    // over the track from the start (or, for a bipolar knob, from the centre).
    const auto& p = *palette;
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f * (float) knobMetrics::margin;
    constexpr float stroke = 2.4f, ringGap = 2.5f;

    const auto centre = bounds.getCentre();
    const auto disc = juce::Rectangle<float> (size, size).withCentre (centre);
    const float ringRadius = size * 0.5f + ringGap;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float middle = (startAngle + endAngle) * 0.5f;
    const bool bipolar = (bool) slider.getProperties().getWithDefault (knobMetrics::bipolar, false);

    if (! slider.isEnabled())
        g.beginTransparencyLayer (0.45f);

    drawRaised (g, disc, size * 0.5f, p, Elevation::small, p.knob);

    const juce::PathStrokeType round (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (track, round);

    const float from = bipolar ? juce::jmin (middle, angle) : startAngle;
    const float to = bipolar ? juce::jmax (middle, angle) : angle;

    if (to - from > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, ringRadius, ringRadius, 0.0f, from, to, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, round);
    }

    const juce::Point<float> inner (centre.x + std::sin (angle) * size * 0.1f, centre.y - std::cos (angle) * size * 0.1f);
    const juce::Point<float> tip (centre.x + std::sin (angle) * size * 0.36f, centre.y - std::cos (angle) * size * 0.36f);
    g.setColour (p.ink);
    g.drawLine (juce::Line<float> (inner, tip), 2.0f);
    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (inner));
    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (tip));

    if (slider.hasKeyboardFocus (false))
        drawFocusRing (g, disc.expanded (2.5f), size * 0.5f + 2.5f, p.ink);

    if (! slider.isEnabled())
        g.endTransparencyLayer();
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
    l->setBorderSize ({ 0, 1, 0, 1 });
    return l;
}

juce::Font ScratchpadLookAndFeel::getLabelFont (juce::Label&)
{
    auto font = uiFont (12.0f);
    return font;
}

void ScratchpadLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Three diagonal strokes in the bottom-right corner; they must still read against the background (3:1).
    g.setColour (isMouseOver || isMouseDragging ? palette->ink : palette->ink2);

    for (const float i : { 0.35f, 0.6f, 0.85f })
        g.drawLine ((float) w * i, (float) h - 2.0f, (float) w - 2.0f, (float) h * i, 1.5f);
}

void ScratchpadLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto bg = label.findColour (juce::Label::backgroundColourId);

    // A knob's value is plain text under the dial (the design has no pill round it).
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) == nullptr && bg.getAlpha() > 0)
    {
        g.setColour (bg);
        g.fillRoundedRectangle (label.getLocalBounds().toFloat(), 6.0f);
    }

    if (label.isBeingEdited())
        return;

    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (label.isEnabled() ? 1.0f : 0.5f));
    // Knob values are narrower than a typical label, so they run a size smaller.
    const bool knobValue = dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr;
    g.setFont (uiFont (knobValue ? 11.0f : 12.0f));
    g.drawText (label.getText(), label.getLocalBounds().reduced (knobValue ? 1 : 4, 0), label.getJustificationType(), false);
}

} // namespace scratchpad::ui
