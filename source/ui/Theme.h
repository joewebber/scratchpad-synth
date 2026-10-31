#pragma once

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui
{

/** One CSS-style box shadow layer: offset, blur radius and colour. */
struct Shadow
{
    float dx, dy, blur;
    juce::Colour colour;
};

/** The pair of layers (a shadow and a highlight) that make a surface look
    raised or pressed in. Same tokens as `--sh-s`, `--sh-l` and `--sh-i` in the design. */
struct ShadowPair
{
    Shadow shade, light;
};

/** Colour tokens for one theme. Values come from the design (`.t-dark` and `.t-light` in
    `docs/design/tidy-full-patch.html`); components read colours only from here, so a theme is just another Palette. */
struct Palette
{
    // Surfaces: the window, a card, the flat tinted zone behind the per-voice modules, an inset well, a knob's disc.
    juce::Colour bg, surface, zone, well, knob;

    // Text, a knob's empty track, the hairline between rows, the dashed outline of an empty slot.
    juce::Colour ink, ink2, mute, track, line, dash;

    // The on-screen keyboard, and the scrim behind a modal layer.
    juce::Colour keyWhite, keyBlack, keyBlackInk, scrim;

    juce::Colour accent, accentSoft, onAccent, accentInk; // accentInk: the accent as text or a line on bg, surface and well

    // Module family colours: fill, and the readable "ink" variant for lines and text.
    juce::Colour src, flt, amp, mod, fx, perf;
    juce::Colour srcInk, fltInk, ampInk, modInk, fxInk, perfInk;

    // Cards and small controls (`--sh-s`), the pop-over (`--sh-l`) and wells, drawn inside their edge (`--sh-i`).
    // `raised` is the old panel shadow, kept equal to `small` until the last panel that asks for it is gone.
    ShadowPair small, lifted, inset, raised;
};

namespace detail
{
    inline juce::Colour rgba (int r, int g, int b, float a) noexcept
    {
        return juce::Colour::fromFloatRGBA ((float) r / 255.0f, (float) g / 255.0f, (float) b / 255.0f, a);
    }
}

inline const Palette& darkPalette()
{
    using detail::rgba;

    static const Palette p = [] {
        const ShadowPair small { { 3, 3, 8, rgba (0, 0, 0, 0.5f) }, { -2, -2, 6, rgba (255, 255, 255, 0.035f) } };

        return Palette {
            .bg = juce::Colour (0xff17181b), .surface = juce::Colour (0xff212226), .zone = juce::Colour (0xff1c1d20),
            .well = juce::Colour (0xff18191c), .knob = juce::Colour (0xff2a2b30),
            .ink = juce::Colour (0xffeeece6), .ink2 = juce::Colour (0xffcbc8c1), .mute = juce::Colour (0xffa4a19a),
            .track = juce::Colour (0xff3a3c42), .line = rgba (255, 255, 255, 0.07f), .dash = juce::Colour (0xff4a4c52),
            .keyWhite = juce::Colour (0xff2e2f34), .keyBlack = juce::Colour (0xff0d0e10), .keyBlackInk = juce::Colour (0xffc9c5bc),
            .scrim = juce::Colour (0x66000000),
            .accent = juce::Colour (0xffe8c83e), .accentSoft = juce::Colour (0xff3a3420), .onAccent = juce::Colour (0xff1e1d1a),
            .accentInk = juce::Colour (0xffe8c83e),
            .src = juce::Colour (0xff8dc2e8), .flt = juce::Colour (0xfff0a3ad), .amp = juce::Colour (0xff8ed3bc),
            .mod = juce::Colour (0xffbcafe4), .fx = juce::Colour (0xfff2bc8f), .perf = juce::Colour (0xff8f877a),
            .srcInk = juce::Colour (0xffa9d3f2), .fltInk = juce::Colour (0xfff6b9c1), .ampInk = juce::Colour (0xffa6e2ce),
            .modInk = juce::Colour (0xffcfc5f0), .fxInk = juce::Colour (0xfff6cda9), .perfInk = juce::Colour (0xffd8d0c2),
            .small = small,
            .lifted = { { 14, 14, 34, rgba (0, 0, 0, 0.65f) }, { -4, -4, 14, rgba (255, 255, 255, 0.04f) } },
            .inset = { { 2, 2, 6, rgba (0, 0, 0, 0.55f) }, { -2, -2, 5, rgba (255, 255, 255, 0.03f) } },
            .raised = small
        };
    }();

    return p;
}

inline const Palette& lightPalette()
{
    using detail::rgba;

    static const Palette p = [] {
        const ShadowPair small { { 3, 3, 7, rgba (110, 100, 80, 0.18f) }, { -3, -3, 7, rgba (255, 255, 255, 0.9f) } };

        return Palette {
            .bg = juce::Colour (0xffe6e5e0), .surface = juce::Colour (0xffeeede9), .zone = juce::Colour (0xffdedcd7),
            .well = juce::Colour (0xffe2e1dc), .knob = juce::Colour (0xfff0efeb),
            .ink = juce::Colour (0xff2e2d2a), .ink2 = juce::Colour (0xff4f4c46), .mute = juce::Colour (0xff615e57),
            .track = juce::Colour (0xffcfccc4), .line = rgba (60, 50, 30, 0.12f), .dash = juce::Colour (0xffb9b5ac),
            .keyWhite = juce::Colour (0xfff7f6f2), .keyBlack = juce::Colour (0xff3a3936), .keyBlackInk = juce::Colour (0xffe6e5e0),
            .scrim = juce::Colour (0x29463e30),
            .accent = juce::Colour (0xffe8c83e), .accentSoft = juce::Colour (0xfff3e7b4), .onAccent = juce::Colour (0xff1e1d1a),
            .accentInk = juce::Colour (0xff7a5f00),
            .src = juce::Colour (0xff8dc2e8), .flt = juce::Colour (0xfff0a3ad), .amp = juce::Colour (0xff8ed3bc),
            .mod = juce::Colour (0xffbcafe4), .fx = juce::Colour (0xfff2bc8f), .perf = juce::Colour (0xffd3cbbc),
            .srcInk = juce::Colour (0xff2b6795), .fltInk = juce::Colour (0xffa54253), .ampInk = juce::Colour (0xff1e6e5b),
            .modInk = juce::Colour (0xff6450b4), .fxInk = juce::Colour (0xff97511e), .perfInk = juce::Colour (0xff6b6253),
            .small = small,
            .lifted = { { 14, 14, 34, rgba (90, 80, 60, 0.28f) }, { -6, -6, 18, rgba (255, 255, 255, 0.8f) } },
            .inset = { { 2, 2, 6, rgba (110, 100, 80, 0.16f) }, { -2, -2, 6, rgba (255, 255, 255, 0.8f) } },
            .raised = small
        };
    }();

    return p;
}

} // namespace scratchpad::ui
