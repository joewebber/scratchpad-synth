#include "ui/Layout.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace scratchpad::ui::layout;

namespace
{
    bool overlaps (const Box& a, const Box& b)
    {
        return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
    }

    std::vector<Box> cards (const Plan& p)
    {
        std::vector<Box> all { p.sources, p.filter, p.filterEnv, p.amp, p.ampEnv, p.out, p.lfo1, p.lfo2, p.playing,
                               p.connections, p.voicingKeyboard, p.explain, p.topBar };
        for (const auto& f : p.fx)
            all.push_back (f);

        return all;
    }
}

TEST_CASE ("The smallest layout is the design, to the pixel", "[layout][plan]")
{
    const auto p = Plan::make (1200, 760, false);

    CHECK (p.explainWidth == 264);
    CHECK (p.columnWidth == 904);

    const std::pair<Box, Box> same[] = {
        { p.sources, sources }, { p.filter, filter }, { p.filterEnv, filterEnv }, { p.amp, amp }, { p.ampEnv, ampEnv },
        { p.out, out }, { p.lfo1, lfo1 }, { p.lfo2, lfo2 }, { p.playing, playing }, { p.connections, connections },
        { p.voicingKeyboard, voicingKeyboard }, { p.explain, explain }, { p.topBar, topBar }, { p.patchView, patchView },
        { p.perVoiceZone, perVoiceZone }, { p.effectsHeading, effectsHeading }, { p.modulationHeading, modulationHeading },
        { p.listenPill, listenPill }, { p.addModuleButton, addModuleButton }, { p.sourcesArrow, sourcesArrow },
        { p.filterAmpArrow, filterAmpArrow }, { p.sum, sum }, { p.fx[0], fx[0] }, { p.fx[3], fx[3] }
    };

    for (const auto& [got, want] : same)
    {
        INFO (want.x << " " << want.y << " " << want.w << " " << want.h);
        CHECK (got.x == want.x);
        CHECK (got.y == want.y);
        CHECK (got.w == want.w);
        CHECK (got.h == want.h);
    }
}

TEST_CASE ("The widths of the other two boards come out as designed", "[layout][plan]")
{
    const auto standard = Plan::make (1440, 900, false);
    CHECK (standard.explainWidth == 300);
    CHECK (standard.columnWidth == 1108);
    CHECK (standard.filter.w == 305);
    CHECK (standard.fx[0].w == 232);
    CHECK (standard.out.w == 112);
    CHECK (standard.lfo1.w == 300);
    CHECK (standard.connections.w == 294);
    CHECK (standard.explain.h == 820);

    const auto wide = Plan::make (1760, 1000, false);
    CHECK (wide.explainWidth == 340);
    CHECK (wide.columnWidth == 1388);
    CHECK (wide.filter.w == 400);
    CHECK (wide.fx[0].w == 300);
    CHECK (wide.out.w == 120);
    CHECK (wide.lfo1.w == 340);
    CHECK (wide.connections.w == 494);
    CHECK (wide.explain.h == 920);
}

TEST_CASE ("Extra width goes to the chain, then the panel, in that order", "[layout][plan]")
{
    const auto at = [] (int w) { return Plan::make (w, 760, false); };

    // Chain first, up to the standard layout's full widths (1108) with the panel at its least.
    CHECK (at (1300).explainWidth == 264);
    CHECK (at (1300).columnWidth == 1300 - 24 - 8 - 264);
    CHECK (at (1404).columnWidth == 1108);

    // Then the panel, to 300.
    CHECK (at (1420).columnWidth == 1108);
    CHECK (at (1420).explainWidth == 1420 - 32 - 1108);

    // Then the chain again, to the wide layout's, then the panel to 340, then the chain takes the rest.
    CHECK (at (1600).explainWidth == 300);
    CHECK (at (1740).columnWidth == 1388);
    CHECK (at (1900).explainWidth == 340);
    CHECK (at (1900).columnWidth == 1900 - 32 - 340);
}

TEST_CASE ("Folding the Explain panel frees its width for the chain", "[layout][plan]")
{
    const auto open = Plan::make (1300, 760, false), folded = Plan::make (1300, 760, true);
    CHECK (folded.explainWidth == railWidth);
    CHECK (folded.columnWidth == open.columnWidth + (open.explainWidth - railWidth));

    // The chain reaches the standard layout's widths (1108) in a window the open panel would leave short of them.
    CHECK (Plan::make (1196, 760, true).columnWidth == 1108);
    CHECK (Plan::make (1196, 760, true).filter.w == 305);
    CHECK (Plan::make (1196, 760, false).filter.w < 305);

    CHECK (Plan::make (992, 760, true).columnWidth == 904);
}

TEST_CASE ("Extra height goes to the zone, the modulation row and the keys, never to the gaps", "[layout][plan]")
{
    for (int h : { 760, 800, 900, 1000, 1200, 1400 })
    {
        const auto p = Plan::make (1440, h, false);
        INFO ("height " << h);

        CHECK (p.extraZone + p.extraModulation + p.extraKeys == h - 760);
        CHECK (p.extraKeys <= maxKeyHeightGrowth);
        CHECK (p.extraModulation <= maxModulationGrowth);

        // The same gaps as the design between every band, and the keyboard card ends at the bottom margin.
        CHECK (p.effectsHeading.y - p.perVoiceZone.bottom() == effectsHeading.y - perVoiceZone.bottom());
        CHECK (p.fx[0].y - p.effectsHeading.y == fx[0].y - effectsHeading.y);
        CHECK (p.modulationHeading.y - p.fx[0].bottom() == modulationHeading.y - fx[0].bottom());
        CHECK (p.lfo1.y - p.modulationHeading.y == lfo1.y - modulationHeading.y);
        CHECK (p.voicingKeyboard.y - p.lfo1.bottom() == voicingKeyboard.y - lfo1.bottom());
        CHECK (p.voicingKeyboard.bottom() == h - margin);
        CHECK (p.explain.bottom() == h - margin);

        // The zone's cards fill it.
        CHECK (p.sources.bottom() == p.perVoiceZone.bottom() - 8);
        CHECK (p.filterEnv.bottom() == p.sources.bottom());
        CHECK (p.filter.bottom() + 8 == p.filterEnv.y);
    }
}

TEST_CASE ("Width and height are separate: a wide, short window keeps the minimum height", "[layout][plan]")
{
    const auto p = Plan::make (1600, 760, false);
    CHECK (p.extraZone == 0);
    CHECK (p.extraKeys == 0);
    CHECK (p.extraModulation == 0);
    CHECK (p.filter.h == filter.h);
    CHECK (p.filter.w > filter.w);
    CHECK (p.voicingKeyboard.h == voicingKeyboard.h);
}

TEST_CASE ("No card overlaps another or leaves the stage, at any size", "[layout][plan]")
{
    for (bool folded : { false, true })
        for (int w : { 992, 1200, 1300, 1440, 1600, 1760, 2000, 2400, 3000 })
            for (int h : { 760, 850, 900, 1000, 1400, 1800 })
            {
                const auto p = Plan::make (w, h, folded);
                INFO (w << " x " << h << (folded ? " folded" : ""));

                const auto all = cards (p);
                for (size_t i = 0; i < all.size(); ++i)
                {
                    CHECK (all[i].x >= p.originX);
                    CHECK (all[i].y >= p.originY);
                    CHECK (all[i].right() <= p.originX + p.width);
                    CHECK (all[i].bottom() <= p.originY + p.height);
                    CHECK (all[i].w > 0);
                    CHECK (all[i].h > 0);

                    for (size_t j = i + 1; j < all.size(); ++j)
                        CHECK_FALSE (overlaps (all[i], all[j]));
                }

                // The chain's three rows end where the column does.
                CHECK (p.amp.right() + 8 == p.perVoiceZone.right());
                CHECK (p.connections.right() == p.perVoiceZone.right());
                CHECK (p.out.right() == p.perVoiceZone.right());
            }
}

TEST_CASE ("Past 2400 x 1400 the layout stops growing and is centred", "[layout][plan]")
{
    const auto p = Plan::make (3000, 1800, false);
    CHECK (p.width == maxLayoutWidth);
    CHECK (p.height == maxLayoutHeight);
    CHECK (p.originX == 300);
    CHECK (p.originY == 200);

    const auto limit = Plan::make (2400, 1400, false);
    CHECK (p.columnWidth == limit.columnWidth);
    CHECK (p.sources.x == limit.sources.x + 300);
    CHECK (p.sources.y == limit.sources.y + 200);
    CHECK (p.explain.right() == 300 + 2400 - margin);
}

TEST_CASE ("A layout under the minimum is laid out at the minimum", "[layout][plan]")
{
    const auto p = Plan::make (800, 500, false);
    CHECK (p.width == minLayoutWidth);
    CHECK (p.height == minLayoutHeight);
    CHECK (Plan::make (800, 500, true).width == minFoldedLayoutWidth);
}
