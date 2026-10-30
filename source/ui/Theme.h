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
    raised or pressed in. Same tokens as `--sh-r`, `--sh-l`, `--sh-s` and
    `--sh-i` in the prototype. */
struct ShadowPair
{
    Shadow shade, light;
};

/** Colour tokens for one theme. Values come from the design prototype
    (`.t-dark` and `.t-light`); components read colours only from here, so a
    theme is just another Palette. */
struct Palette
{
    juce::Colour bg, surface, well, ink, ink2, mute, track, dash, keyWhite, keyBlack, scrim, keyBlackInk;
    juce::Colour accent, accentSoft, onAccent, accentInk; // accentInk: the accent as text or a line on bg, surface and well

    // Module family colours: fill, and the readable "ink" variant for lines and text.
    juce::Colour src, flt, amp, mod, fx, perf;
    juce::Colour srcInk, fltInk, ampInk, modInk, fxInk, perfInk;

    // Panels, lifted cards, small controls, and wells (drawn inside their edge).
    ShadowPair raised, lifted, small, inset;
};

inline const Palette& darkPalette()
{
    static const Palette p {
        juce::Colour (0xff18191c), juce::Colour (0xff1f2024), juce::Colour (0xff16171a),
        juce::Colour (0xffeeece6), juce::Colour (0xffcbc8c1), juce::Colour (0xffa4a19a),
        juce::Colour (0xff36383d), juce::Colour (0xff4a4c52), juce::Colour (0xff2c2d32),
        juce::Colour (0xff0d0e10), juce::Colour (0x66000000), juce::Colour (0xffc9c5bc),
        juce::Colour (0xffe8c83e), juce::Colour (0xff3a3420), juce::Colour (0xff1e1d1a), juce::Colour (0xffe8c83e),
        juce::Colour (0xff8dc2e8), juce::Colour (0xfff0a3ad), juce::Colour (0xff8ed3bc),
        juce::Colour (0xffbcafe4), juce::Colour (0xfff2bc8f), juce::Colour (0xff8f877a),
        juce::Colour (0xffa9d3f2), juce::Colour (0xfff6b9c1), juce::Colour (0xffa6e2ce),
        juce::Colour (0xffcfc5f0), juce::Colour (0xfff6cda9), juce::Colour (0xffd8d0c2),
        { { 6, 6, 14, juce::Colour::fromFloatRGBA (0, 0, 0, 0.55f) },   { -5, -5, 12, juce::Colour::fromFloatRGBA (1, 1, 1, 0.035f) } },
        { { 14, 14, 34, juce::Colour::fromFloatRGBA (0, 0, 0, 0.7f) },  { -6, -6, 18, juce::Colour::fromFloatRGBA (1, 1, 1, 0.04f) } },
        { { 3, 3, 7, juce::Colour::fromFloatRGBA (0, 0, 0, 0.55f) },    { -2, -2, 6, juce::Colour::fromFloatRGBA (1, 1, 1, 0.04f) } },
        { { 3, 3, 7, juce::Colour::fromFloatRGBA (0, 0, 0, 0.55f) },    { -2, -2, 6, juce::Colour::fromFloatRGBA (1, 1, 1, 0.03f) } }
    };
    return p;
}

inline const Palette& lightPalette()
{
    static const Palette p {
        juce::Colour (0xffe6e5e0), juce::Colour (0xffecebe7), juce::Colour (0xffe2e1dc),
        juce::Colour (0xff2e2d2a), juce::Colour (0xff4f4c46), juce::Colour (0xff615e57),
        juce::Colour (0xffd3d1ca), juce::Colour (0xffb9b5ac), juce::Colour (0xfff5f4f0),
        juce::Colour (0xff3a3936), juce::Colour (0x29463e30), juce::Colour (0xffe6e5e0),
        juce::Colour (0xffe8c83e), juce::Colour (0xfff3e7b4), juce::Colour (0xff1e1d1a), juce::Colour (0xff7a5f00),
        juce::Colour (0xff8dc2e8), juce::Colour (0xfff0a3ad), juce::Colour (0xff8ed3bc),
        juce::Colour (0xffbcafe4), juce::Colour (0xfff2bc8f), juce::Colour (0xffd3cbbc),
        juce::Colour (0xff2b6794), juce::Colour (0xffa44153), juce::Colour (0xff1e6f5b),
        juce::Colour (0xff6450b4), juce::Colour (0xff96501e), juce::Colour (0xff6b6253),
        { { 6, 6, 14, juce::Colour (110, 100, 80).withAlpha (0.20f) },  { -6, -6, 14, juce::Colour::fromFloatRGBA (1, 1, 1, 0.9f) } },
        { { 14, 14, 34, juce::Colour (90, 80, 60).withAlpha (0.28f) },  { -8, -8, 22, juce::Colour::fromFloatRGBA (1, 1, 1, 0.8f) } },
        { { 3, 3, 7, juce::Colour (110, 100, 80).withAlpha (0.20f) },   { -3, -3, 7, juce::Colour::fromFloatRGBA (1, 1, 1, 0.9f) } },
        { { 3, 3, 7, juce::Colour (110, 100, 80).withAlpha (0.17f) },   { -3, -3, 7, juce::Colour::fromFloatRGBA (1, 1, 1, 0.85f) } }
    };
    return p;
}

} // namespace scratchpad::ui
