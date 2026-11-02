#pragma once

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui::layout
{

/** The stage every card is laid out on, in layout pixels. The numbers below are the design at its smallest layout,
    1200 x 760 (the "compact" one): the least a card is ever drawn at. A bigger window gives `Plan` (further down) bigger
    boxes by the rules there; a card lays itself out from the box it is given (its own width and height) and picks
    between its compact and its full layout by whole (`layout::full`). Nothing sizes itself from its content (a control
    that does not fit goes behind the card's More button, it does not make the card bigger).

    Layout pixels are not screen pixels: the editor puts one scale transform (the UI scale, the user's setting) over
    the whole stage, so a knob is the same number of layout pixels at every window size.

    Numbers are read off `docs/design/tidy-full-patch.html` (the stage's own flex layout, resolved to pixels). */
inline constexpr int stageWidth = 1200, stageHeight = 760;

/** x, y, width, height in stage pixels. Converts to a `juce::Rectangle<int>` wherever one is expected. */
struct Box
{
    int x, y, w, h;

    constexpr int right() const noexcept { return x + w; }
    constexpr int bottom() const noexcept { return y + h; }

    juce::Rectangle<int> rect() const noexcept { return { x, y, w, h }; }
    operator juce::Rectangle<int>() const noexcept { return rect(); }
};

//==============================================================================
// Regions of the stage.

inline constexpr int margin = 12, gap = 8;

inline constexpr Box topBar { 12, 12, 1176, 48 };

/** What `PatchView` covers: the signal chain, effects and modulation (everything between the top bar and the
    voicing-and-keyboard card, left of the Explain panel). Its children are placed from the stage boxes below,
    less this box's origin. */
inline constexpr Box patchView { 12, 68, 904, 572 };
inline constexpr Box explain { 924, 68, 264, 680 };

// Signal chain: heading row, then the per-voice zone.
inline constexpr Box signalChainHeading { 12, 68, 904, 22 };   // SIGNAL CHAIN, the Listen-for pill, "+ Add module"
inline constexpr Box listenPill { 150, 68, 652, 22 };           // radius 11, accent-soft; starts 14 px after the heading text
inline constexpr Box addModuleButton { 816, 68, 96, 22 };       // radius 11, 12 px text
inline constexpr Box perVoiceZone { 12, 96, 904, 306 };         // radius 16, `zone` fill; a 24 px strip on the left holds the vertical caption
inline constexpr Box perVoiceCaption { 12, 96, 24, 306 };       // "PER VOICE · ONE COPY PER NOTE", reads bottom to top

inline constexpr Box sources { 36, 104, 368, 290 };
inline constexpr Box sourcesArrow { 404, 104, 18, 290 };        // a chevron between the columns: the signal runs left to right
inline constexpr Box filter { 422, 104, 234, 186 };
inline constexpr Box filterEnv { 422, 298, 234, 96 };
inline constexpr Box filterAmpArrow { 656, 104, 18, 290 };
inline constexpr Box amp { 674, 104, 234, 186 };
inline constexpr Box ampEnv { 674, 298, 234, 96 };

// Effects.
inline constexpr Box effectsHeading { 12, 412, 904, 18 };       // EFFECTS, then a one-line hint
inline constexpr Box sum { 12, 436, 28, 80 };                   // the Σ disc (26 px, centred): voices are added together here
inline constexpr int numFx = 4;
inline constexpr Box fx[numFx] {
    { 48, 436, 184, 80 }, { 240, 436, 184, 80 }, { 432, 436, 184, 80 }, { 624, 436, 184, 80 }
};
inline constexpr Box out { 816, 436, 100, 80 };

// Modulation.
inline constexpr Box modulationHeading { 12, 526, 904, 18 };    // MODULATION, then a one-line hint
inline constexpr Box lfo1 { 12, 550, 214, 88 };
inline constexpr Box lfo2 { 234, 550, 214, 88 };
inline constexpr Box playing { 456, 550, 190, 88 };
inline constexpr Box connections { 654, 550, 262, 88 };         // shows three rows, scrolls past that

inline constexpr Box voicingKeyboard { 12, 648, 904, 100 };

/** The two-line section heading's text: 12 px, upper case, letter-spacing 0.24em, medium, `ink`. The hint after it is 12 px `mute`. */
inline constexpr int headingTextInset = 4;                      // the heading rows have 4 px side padding
inline constexpr int headingGap = 14;                           // between the heading and its hint

//==============================================================================
// Inside the cards (all numbers are pixels at 100%).

/** Header row of every card: handle (20 px dot, or the 22 px drag handle of a source), name, "?" 15 px, and right
    aligned B, S (22 x 20) and the 22 x 20 More button, 4 px apart. */
namespace header
{
    inline constexpr int height = 20, lfoHeight = 22, gap = 5;
    inline constexpr int dot = 20, handle = 22, explain = 15;
    inline constexpr int buttonWidth = 22, buttonHeight = 20, buttonGap = 4;
    inline constexpr float nameSize = 13.0f;
}

/** Card padding, as the HTML gives it. */
namespace padding
{
    inline constexpr int sources = 10, filter = 9, env = 9, fx = 7, lfo = 8, playing = 8, voicing = 10;
}

/** The oscillator table inside the Sources card. */
namespace table
{
    inline constexpr int headingHeight = 14, headingMargin = 6, rowHeight = 60, nameColumn = 60, rowGap = 6;
    inline constexpr int waveWidth = 52, waveHeight = 44, waveWellHeight = 30;
    inline constexpr int cell = 38;                              // WAVE, OCT, SEMI, FINE, LEVEL and MORPH columns are 38 px apart (WAVE is 52)
    inline constexpr int noiseCell = 44;
}

/** A knob is its disc diameter plus the width of the cell it sits in (the caption or value is centred in it). */
struct KnobSize
{
    int diameter, cell;
};

namespace knobs
{
    inline constexpr KnobSize table { 24, 38 };       // oscillator table
    inline constexpr KnobSize noiseLevel { 24, 44 };
    inline constexpr KnobSize cutoff { 28, 48 };
    inline constexpr KnobSize standard { 24, 40 };    // filter Res / Key / Drive, amp Level / Pan
    inline constexpr KnobSize envelope { 22, 30 };
    inline constexpr KnobSize effect { 22, 34 };
    inline constexpr KnobSize lfo { 22, 34 };
    inline constexpr KnobSize out { 26, 44 };
    inline constexpr KnobSize voicing { 22, 35 };
    inline constexpr KnobSize popover { 26, 52 };     // label and value are both shown
}

/** Widths of the segmented controls, so the fit test (`tests/ChoiceFitTests.cpp`) can check that every option fits its
    cell. A card registers its segments here. */
namespace segments
{
    inline constexpr int filterMode = 216;            // the whole width of the Filter card's content
    inline constexpr int noiseColour = 110;
    inline constexpr int voiceMode = 132;
    inline constexpr int popoverRow = 260;            // the pop-over is 288 wide with 14 px padding
}

//==============================================================================
// The More pop-over.
namespace popover
{
    inline constexpr int width = 288, padding = 14, radius = 16, gap = 10;
    inline constexpr int headerHeight = 22, removeHeight = 30;
}

//==============================================================================
// Bigger windows.

/** Where a card changes whole from its compact layout to its full one. A card is full when its own box is at least
    this wide (and tall, where a height is given), never part way. The card reads its own `getWidth()`/`getHeight()`;
    nothing else tells it. */
namespace full
{
    inline constexpr int filterWidth = 305, filterHeight = 232;  // the shaper (Shape, Bias, Mix) comes onto the card
    inline constexpr int effectWidth = 232;                      // each effect's fourth control; Delay's Sync and Ping-pong
    inline constexpr int lfoWidth = 300;                         // the shape strip and Retrig
    inline constexpr int connectionsColumnsWidth = 400;          // the list in two columns
    inline constexpr int connectionsTallHeight = 104;            // four rows to a column: two columns show all eight slots
}

/** What the window is allowed to be, in layout pixels. Under the minimum the editor shrinks the whole stage instead of
    rearranging it (never below 75% of a screen pixel). Over the maximum the layout stops growing and is centred. */
inline constexpr int minLayoutWidth = 1200, minFoldedLayoutWidth = 992, minLayoutHeight = 760;
inline constexpr int maxLayoutWidth = 2400, maxLayoutHeight = 1400;

inline constexpr int railWidth = 56;                             // the Explain panel folded to its rail
inline constexpr int explainWidthCompact = 264, explainWidthStandard = 300, explainWidthWide = 340;
inline constexpr int maxKeyHeightGrowth = 50;                    // the on-screen keys are 70 px at the least, 120 at the most
inline constexpr int maxModulationGrowth = 16;                   // the modulation row is 88 px tall at the least, 104 at the most

/** Every rectangle of the stage for one window, worked out from the layout size alone (window size / UI scale).

    Width. The window's width less the margins is shared between the Explain panel and the left column (the chain, the
    effects, the modulation and the keyboard) in this order: the column grows to the standard layout's full widths
    (1108), the panel to 300, the column to the wide layout's (1388), the panel to 340, then the column takes the rest
    (the keyboard fits more octaves and the cards' visuals widen). Folded to its rail the panel stays 56 and the column
    takes everything, so the chain reaches a bigger layout in a smaller window. Inside the column each row is laid out
    from keyframes at the three column widths (904, 1108, 1388): the cards' widths are interpolated between them.

    Height. The rows have their compact heights plus the extra height above 760, shared: the keyboard a quarter (its
    keys at most 50 px taller), the modulation row a quarter (at most 16 px), the per-voice zone the rest. Of the
    zone's extra, the Filter and Amp take 70% and their envelopes the rest. The Explain panel is as tall as the window
    and gives all of its extra height to its scope. Gaps, margins, headers, knobs and text never stretch.

    Past 2400 x 1400 the layout stops growing and sits in the middle of the window. The boxes are in stage pixels,
    including that offset. Both dimensions are worked out separately, so a wide, short window gets the width and none of
    the height. */
struct Plan
{
    int width = minLayoutWidth, height = minLayoutHeight;       ///< the layout's size after the maximum
    int originX = 0, originY = 0;                               ///< where it starts in the stage
    bool folded = false;
    int columnWidth = 0, explainWidth = 0;
    int extraZone = 0, extraModulation = 0, extraKeys = 0;      ///< the extra height each band got

    Box topBar {}, patchView {}, explain {}, voicingKeyboard {};
    Box signalChainHeading {}, listenPill {}, addModuleButton {}, perVoiceZone {}, perVoiceCaption {};
    Box sources {}, sourcesArrow {}, filter {}, filterEnv {}, filterAmpArrow {}, amp {}, ampEnv {};
    Box effectsHeading {}, sum {}, fx[numFx] {}, out {};
    Box modulationHeading {}, lfo1 {}, lfo2 {}, playing {}, connections {};

    /** The smallest layout width: the design's, or with the Explain panel folded, less what the rail leaves free. */
    static constexpr int minWidthFor (bool explainFolded) noexcept { return explainFolded ? minFoldedLayoutWidth : minLayoutWidth; }

    static constexpr Plan make (int layoutWidth, int layoutHeight, bool explainFolded) noexcept
    {
        Plan p;
        p.folded = explainFolded;

        const int fullW = layoutWidth < minWidthFor (explainFolded) ? minWidthFor (explainFolded) : layoutWidth;
        const int fullH = layoutHeight < minLayoutHeight ? minLayoutHeight : layoutHeight;
        p.width = fullW > maxLayoutWidth ? maxLayoutWidth : fullW;
        p.height = fullH > maxLayoutHeight ? maxLayoutHeight : fullH;
        p.originX = (fullW - p.width) / 2;
        p.originY = (fullH - p.height) / 2;

        // Width: the column and the Explain panel.
        const int shared = p.width - 2 * margin - gap;
        int column = 0, panel = 0;

        if (explainFolded)                              { panel = railWidth;             column = shared - panel; }
        else if (shared <= 1108 + explainWidthCompact)  { panel = explainWidthCompact;   column = shared - panel; }
        else if (shared <= 1108 + explainWidthStandard) { column = 1108;                 panel = shared - column; }
        else if (shared <= 1388 + explainWidthStandard) { panel = explainWidthStandard;  column = shared - panel; }
        else if (shared <= 1388 + explainWidthWide)     { column = 1388;                 panel = shared - column; }
        else                                            { panel = explainWidthWide;      column = shared - panel; }

        p.columnWidth = column;
        p.explainWidth = panel;

        // Height: the three bands that take extra.
        const int extra = p.height - minLayoutHeight;
        p.extraKeys = extra / 4 < maxKeyHeightGrowth ? extra / 4 : maxKeyHeightGrowth;
        p.extraModulation = extra / 4 < maxModulationGrowth ? extra / 4 : maxModulationGrowth;
        p.extraZone = extra - p.extraKeys - p.extraModulation;
        const int filterShare = p.extraZone * 7 / 10;

        // Keyframes at the column widths 904, 1108 and 1388, read off the three boards, interpolated.
        const auto between = [column] (int atCompact, int atStandard, int atWide)
        {
            const bool low = column < 1108;
            const int c0 = low ? 904 : 1108, c1 = low ? 1108 : 1388, v0 = low ? atCompact : atStandard, v1 = low ? atStandard : atWide;
            return v0 + ((v1 - v0) * (column - c0)) / (c1 - c0);
        };

        const int x0 = p.originX + margin, y0 = p.originY;
        const auto box = [] (int x, int y, int w, int h) { return Box { x, y, w, h }; };

        p.topBar = box (x0, y0 + 12, p.width - 2 * margin, 48);

        // Signal chain.
        const int zoneY = y0 + 96, zoneH = 306 + p.extraZone;
        const int filterW = between (234, 305, 400);
        const int sourcesW = column - 32 - 2 * filterW - 36;
        const int sourcesX = x0 + 24, filterX = sourcesX + sourcesW + 18, ampX = filterX + filterW + 18;
        const int top = zoneY + 8;

        p.signalChainHeading = box (x0, y0 + 68, column, 22);
        p.addModuleButton = box (x0 + column - 4 - 96, y0 + 68, 96, 22);
        p.listenPill = box (x0 + 138, y0 + 68, p.addModuleButton.x - (x0 + 138) - 14, 22);
        p.perVoiceZone = box (x0, zoneY, column, zoneH);
        p.perVoiceCaption = box (x0, zoneY, 24, zoneH);
        p.sources = box (sourcesX, top, sourcesW, 290 + p.extraZone);
        p.sourcesArrow = box (sourcesX + sourcesW, top, 18, 290 + p.extraZone);
        p.filter = box (filterX, top, filterW, 186 + filterShare);
        p.filterEnv = box (filterX, top + 194 + filterShare, filterW, 96 + p.extraZone - filterShare);
        p.filterAmpArrow = box (filterX + filterW, top, 18, 290 + p.extraZone);
        p.amp = box (ampX, top, filterW, 186 + filterShare);
        p.ampEnv = box (ampX, top + 194 + filterShare, filterW, 96 + p.extraZone - filterShare);

        // Effects.
        const int effectsY = zoneY + zoneH + 10, fxY = effectsY + 24;
        const int outW = between (100, 112, 120);
        const int fxW = (column - 68 - outW) / 4;
        const int outWide = column - 68 - 4 * fxW; // what the four cards leave over is Out's

        p.effectsHeading = box (x0, effectsY, column, 18);
        p.sum = box (x0, fxY, 28, 80);
        for (int i = 0; i < numFx; ++i)
            p.fx[i] = box (x0 + 36 + i * (fxW + gap), fxY, fxW, 80);
        p.out = box (x0 + 36 + numFx * (fxW + gap), fxY, outWide, 80);

        // Modulation.
        const int modHeadingY = fxY + 80 + 10, modY = modHeadingY + 24, modH = 88 + p.extraModulation;
        const int lfoW = between (214, 300, 340);
        const int connectionsX = x0 + 2 * lfoW + 2 * gap + 190 + gap;

        p.modulationHeading = box (x0, modHeadingY, column, 18);
        p.lfo1 = box (x0, modY, lfoW, modH);
        p.lfo2 = box (x0 + lfoW + gap, modY, lfoW, modH);
        p.playing = box (x0 + 2 * lfoW + 2 * gap, modY, 190, modH);
        p.connections = box (connectionsX, modY, x0 + column - connectionsX, modH);

        const int keyboardY = modY + modH + 10;
        p.voicingKeyboard = box (x0, keyboardY, column, 100 + p.extraKeys);
        p.patchView = box (x0, y0 + 68, column, modY + modH + 2 - (y0 + 68));
        p.explain = box (x0 + column + gap, y0 + 68, panel, p.height - 80);

        return p;
    }
};

} // namespace scratchpad::ui::layout
