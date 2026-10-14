#pragma once

#include "ui/LookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** A row of flat pills in an inset well, one per choice of a choice parameter;
    the chosen one is filled with the module's family colour. Segments show
    either the choice text or a stroked glyph (18 x 10 px viewBox).

    Mouse and the left / right arrow keys pick. A pick is one undo step named
    "<Caption> <old> -> <new>", unless `onPick` is set, which then does the
    picking instead (oscillator 1's wave goes through the processor). */
class ChoiceSegments final : public juce::Component
{
public:
    ChoiceSegments (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                    const juce::String& captionText, juce::UndoManager& undoToUse,
                    juce::Colour Palette::* familyColour)
        : caption (captionText), undo (undoToUse), family (familyColour),
          param (state.getParameter (paramID))
    {
        jassert (param != nullptr);

        setPaintingIsUnclipped (true);
        setWantsKeyboardFocus (true);
        setTitle (caption);

        const auto count = (int) std::round (param->getNormalisableRange().end) + 1;
        for (int i = 0; i < count; ++i)
            labels.add (param->getText (param->convertTo0to1 ((float) i), 0));

        attachment = std::make_unique<juce::ParameterAttachment> (
            *param, [this] (float v) { setSelected ((int) std::round (v)); }, &undo);
        attachment->sendInitialUpdate();
    }

    /** Draw these glyphs instead of the choice text. One path per choice. */
    void setGlyphs (const std::vector<const char*>& svgPaths)
    {
        glyphs.clear();
        for (auto* d : svgPaths)
            glyphs.push_back (juce::Drawable::parseSVGPath (d));
        repaint();
    }

    /** Called with the picked index instead of writing the parameter. */
    std::function<void (int)> onPick;

    int getSelected() const noexcept { return selected; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        const auto bounds = getLocalBounds().toFloat();
        drawInset (g, bounds, 9.0f, p, p.well);

        for (int i = 0; i < labels.size(); ++i)
        {
            const auto seg = segmentBounds (i);
            const bool on = i == selected;

            if (on)
            {
                g.setColour (p.*family);
                g.fillRoundedRectangle (seg, 7.0f);
            }
            else if (i == hovered)
            {
                g.setColour (p.ink.withAlpha (0.06f));
                g.fillRoundedRectangle (seg, 7.0f);
            }

            g.setColour (on ? p.onAccent : p.ink2);

            if (i < (int) glyphs.size())
            {
                auto path = glyphs[(size_t) i];
                path.applyTransform (juce::AffineTransform::translation (seg.getCentreX() - 9.0f, seg.getCentreY() - 5.0f));
                g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            }
            else
            {
                auto font = uiFont (11.0f, Weight::medium);
                font.setExtraKerningFactor (0.06f);
                g.setFont (font);
                g.drawText (labels[i].toUpperCase(), seg, juce::Justification::centred, false);
            }
        }

        if (hasKeyboardFocus (false))
            ScratchpadLookAndFeel::drawFocusRing (g, bounds, 9.0f, p.ink);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = indexAt (e.position);
        if (i >= 0)
            pick (i);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey && selected > 0)
            pick (selected - 1);
        else if (key == juce::KeyPress::rightKey && selected < labels.size() - 1)
            pick (selected + 1);
        else
            return false;

        return true;
    }

private:
    juce::Rectangle<float> segmentBounds (int i) const
    {
        constexpr float inset = 2.0f, gap = 2.0f;
        const auto inner = getLocalBounds().toFloat().reduced (inset);
        const float w = (inner.getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
        return { inner.getX() + (float) i * (w + gap), inner.getY(), w, inner.getHeight() };
    }

    int indexAt (juce::Point<float> position) const
    {
        for (int i = 0; i < labels.size(); ++i)
            if (segmentBounds (i).expanded (1.0f, 2.0f).contains (position))
                return i;

        return -1;
    }

    void setHovered (int i)
    {
        if (i != hovered)
        {
            hovered = i;
            repaint();
        }
    }

    void setSelected (int i)
    {
        selected = juce::jlimit (0, juce::jmax (0, labels.size() - 1), i);
        repaint();
    }

    void pick (int i)
    {
        if (i == selected)
            return;

        if (onPick)
        {
            onPick (i);
            return;
        }

        const auto old = labels[selected];
        attachment->setValueAsCompleteGesture ((float) i);
        undo.setCurrentTransactionName (caption + " " + old + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + labels[i]);
    }

    juce::String caption;
    juce::UndoManager& undo;
    juce::Colour Palette::* family;
    juce::RangedAudioParameter* param;
    juce::StringArray labels;
    std::vector<juce::Path> glyphs;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hovered = -1;
};

} // namespace scratchpad::ui
