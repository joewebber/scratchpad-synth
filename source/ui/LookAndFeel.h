#pragma once

#include "ui/Neumorph.h"
#include "ui/Contrast.h"
#include "ui/Theme.h"
#include "ui/Typography.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** Property names read from a button's `getProperties()`. */
namespace buttonProps
{
    inline constexpr const char* radius = "radius";     // corner radius in px, default min(10, h * 0.35)
    inline constexpr const char* tracking = "tracking"; // letter spacing in em
    inline constexpr const char* fontSize = "fontSize"; // px, default 12
}

class ScratchpadLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit ScratchpadLookAndFeel (const Palette& initial);

    void setPalette (const Palette& p);
    const Palette& getPalette() const noexcept { return *palette; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    /** The plugin window's resize grip, in palette colours. */
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    /** Rounded rectangle with a visible keyboard-focus ring, used by every control. */
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float> bounds, float radius, juce::Colour);

    /** A thin outline round a filled "selected" or "on" shape, drawn only where the fill is too close to what it sits on
        (`behind`) to be told apart, as the pale family colours are on the light theme. */
    static void drawSelectedEdge (juce::Graphics&, juce::Rectangle<float> bounds, float radius, const Palette&,
                                  juce::Colour fill, juce::Colour behind);

private:
    void applyColours();

    const Palette* palette;
};

/** The palette of the nearest ScratchpadLookAndFeel, falling back to dark. */
inline const Palette& paletteOf (const juce::Component& c)
{
    if (auto* laf = dynamic_cast<ScratchpadLookAndFeel*> (&c.getLookAndFeel()))
        return laf->getPalette();

    return darkPalette();
}

} // namespace scratchpad::ui
