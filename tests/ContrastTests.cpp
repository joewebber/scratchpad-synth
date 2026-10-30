#include "ui/Contrast.h"
#include "ui/Theme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <vector>

using namespace scratchpad::ui;
using Catch::Matchers::WithinAbs;

namespace
{
    struct Pair
    {
        const char* what;
        juce::Colour Palette::*foreground;
        juce::Colour Palette::*background;
    };

    struct Named
    {
        const char* name;
        const Palette& palette;
    };

    const Named palettes[] = { { "dark", darkPalette() }, { "light", lightPalette() } };

    /** Text the UI really draws, on the surfaces it really draws it on. Each needs 4.5:1. */
    const std::vector<Pair>& textPairs()
    {
        static const std::vector<Pair> pairs = [] {
            std::vector<Pair> v;

            // Body text: names, values, captions, hints, on every surface a panel, well or window uses.
            for (auto fg : { &Palette::ink, &Palette::ink2, &Palette::mute })
                for (auto bg : { &Palette::bg, &Palette::surface, &Palette::well })
                    v.push_back ({ "body text", fg, bg });

            // Link chips and the Listen pill: ink text on the soft accent.
            for (auto fg : { &Palette::ink, &Palette::ink2, &Palette::mute })
                v.push_back ({ "text on soft accent", fg, &Palette::accentSoft });

            // Family ink: module titles, kicker text, curves and arcs, on panels and wells.
            for (auto fg : { &Palette::srcInk, &Palette::fltInk, &Palette::ampInk, &Palette::modInk, &Palette::fxInk, &Palette::perfInk })
                for (auto bg : { &Palette::bg, &Palette::surface, &Palette::well })
                    v.push_back ({ "family ink", fg, bg });

            // Dark text on every fill that carries a label: selected segments, tags, the accent button, key presses.
            for (auto bg : { &Palette::accent, &Palette::src, &Palette::flt, &Palette::amp, &Palette::mod, &Palette::fx, &Palette::perf })
                v.push_back ({ "text on a fill", &Palette::onAccent, bg });

            // The accent as text (the suggested slot's label) and as a line.
            for (auto bg : { &Palette::bg, &Palette::surface, &Palette::well })
                v.push_back ({ "accent ink", &Palette::accentInk, bg });

            // Keyboard labels.
            v.push_back ({ "white key label", &Palette::mute, &Palette::keyWhite });
            v.push_back ({ "white key label", &Palette::ink, &Palette::keyWhite });
            v.push_back ({ "black key label", &Palette::keyBlackInk, &Palette::keyBlack });

            return v;
        }();

        return pairs;
    }

    /** Parts of a control that carry meaning without text: value arcs and curves, the focus ring, the meter, the resize
        grip. Each needs 3:1. */
    const std::vector<Pair>& graphicPairs()
    {
        static const std::vector<Pair> pairs = [] {
            std::vector<Pair> v;

            for (auto fg : { &Palette::srcInk, &Palette::fltInk, &Palette::ampInk, &Palette::modInk, &Palette::fxInk, &Palette::perfInk,
                             &Palette::accentInk })
                for (auto bg : { &Palette::bg, &Palette::surface, &Palette::well })
                    v.push_back ({ "arc, curve or marker", fg, bg });

            // The focus ring and the knob's pointer line are drawn in ink; the grip in ink2.
            for (auto bg : { &Palette::bg, &Palette::surface, &Palette::well, &Palette::keyWhite })
                v.push_back ({ "focus ring", &Palette::ink, bg });

            for (auto bg : { &Palette::bg, &Palette::surface })
                v.push_back ({ "resize grip", &Palette::ink2, bg });

            return v;
        }();

        return pairs;
    }
}

TEST_CASE ("The contrast maths matches WCAG's own examples", "[contrast]")
{
    CHECK_THAT (contrastRatio (juce::Colours::black, juce::Colours::white), WithinAbs (21.0, 1.0e-3));
    CHECK_THAT (contrastRatio (juce::Colours::white, juce::Colours::white), WithinAbs (1.0, 1.0e-6));

    // #767676 on white is the usual "just passes" grey: 4.54:1.
    CHECK_THAT (contrastRatio (juce::Colour (0xff767676), juce::Colours::white), WithinAbs (4.54, 0.01));

    // Order does not matter.
    CHECK_THAT (contrastRatio (juce::Colours::white, juce::Colour (0xff767676)), WithinAbs (4.54, 0.01));
}

TEST_CASE ("Text meets WCAG 2.2 AA (4.5:1) in both themes", "[contrast]")
{
    for (const auto& named : palettes)
        for (const auto& pair : textPairs())
        {
            const auto fg = named.palette.*pair.foreground, bg = named.palette.*pair.background;
            const float ratio = contrastRatio (fg, bg);

            INFO (named.name << ": " << pair.what << " " << fg.toDisplayString (false) << " on " << bg.toDisplayString (false) << " = " << ratio);
            CHECK (ratio >= minTextContrast);
        }
}

TEST_CASE ("Arcs, rings and markers meet 3:1 in both themes", "[contrast]")
{
    for (const auto& named : palettes)
        for (const auto& pair : graphicPairs())
        {
            const auto fg = named.palette.*pair.foreground, bg = named.palette.*pair.background;
            const float ratio = contrastRatio (fg, bg);

            INFO (named.name << ": " << pair.what << " " << fg.toDisplayString (false) << " on " << bg.toDisplayString (false) << " = " << ratio);
            CHECK (ratio >= minGraphicContrast);
        }
}

TEST_CASE ("The light theme is light and the dark theme is dark", "[contrast]")
{
    CHECK (relativeLuminance (lightPalette().bg) > 0.6f);
    CHECK (relativeLuminance (lightPalette().ink) < 0.1f);
    CHECK (relativeLuminance (darkPalette().bg) < 0.05f);
    CHECK (relativeLuminance (darkPalette().ink) > 0.6f);
}
