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

/** How a rotary knob's slider is laid out: the disc's cell (a square, disc diameter + 6) at the top, centred, and the
    value box (the one a click turns into a typed entry) a line below it. A `Knob` sets the diameter and the box's
    position as properties on its slider. */
namespace knobMetrics
{
    inline constexpr const char* discSize = "knobDisc";       // int: the disc's diameter in px
    inline constexpr const char* valueY = "knobValueY";       // int: top of the value box; default is under the disc
    inline constexpr const char* bipolar = "knobBipolar";     // bool: the value arc runs from the centre
    inline constexpr int margin = 3;                          // from the disc to the edge of its square cell
    inline constexpr int labelGap = 3, labelHeight = 13;      // the caption or value line under the disc
    inline constexpr int defaultDisc = 24;
}

class ScratchpadLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit ScratchpadLookAndFeel (const Palette& initial);

    void setPalette (const Palette& p);
    const Palette& getPalette() const noexcept { return *palette; }

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getLabelFont (juce::Label&) override;

    /** A call-out box (the More pop-over) is a lifted rounded panel with room round it for its shadow, no arrow. */
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize (const juce::CallOutBox&) override { return 24; }

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
