#pragma once

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui::layout
{

/** The stage every card is laid out on: 1200 x 760 px at 100%, scaled as one unit by the editor. This header is the
    single source of truth for sizes; a card's `width` and `height` come from here, and nothing may size itself from
    its content (a control that does not fit goes behind the card's More button, it does not make the card bigger).

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
// A taller window.

/** The stage is never scaled up: a window taller than the design gives the extra height to the cards, and the gaps
    stay as they are. `forExtra` splits the extra height between the four bands that stack down the left column;
    `grown` gives a design box its size in the taller stage. Within the per-voice zone the Filter and the Amp (and the
    zone and the arrows between the columns) take their share, and the envelopes under them theirs. A card is handed
    its grown box and is expected to lay out from its own height: its one flexible part (a visualisation, the table
    rows, a meter) absorbs the difference and everything else keeps its size.

    Fractions of the extra: the zone half, the effects a tenth, the modulation row a fifth, the keyboard a fifth. */
struct Grow
{
    int zone = 0, effects = 0, modulation = 0, keyboard = 0;
    int zoneTop = 0; ///< of `zone`, what the Filter and Amp take (the envelopes under them take the rest)

    static constexpr Grow forExtra (int extraHeight) noexcept
    {
        Grow g;
        const int e = extraHeight > 0 ? extraHeight : 0;
        g.zone = e * 5 / 10;
        g.effects = e / 10;
        g.modulation = e / 5;
        g.keyboard = e / 5;
        g.zoneTop = g.zone * 6 / 10;
        return g;
    }

    constexpr int total() const noexcept { return zone + effects + modulation + keyboard; }
};

constexpr bool operator== (const Box& a, const Box& b) noexcept { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

/** `box` (one of the design's regions above) in a stage taller by `g`. */
constexpr Box grown (const Box& box, const Grow& g) noexcept
{
    // The per-voice zone: the Sources card, the columns' arrows, the zone itself and its caption take all of `zone`.
    if (box == sources || box == sourcesArrow || box == filterAmpArrow || box == perVoiceZone || box == perVoiceCaption)
        return { box.x, box.y, box.w, box.h + g.zone };

    if (box == filter || box == amp)
        return { box.x, box.y, box.w, box.h + g.zoneTop };

    if (box == filterEnv || box == ampEnv)
        return { box.x, box.y + g.zoneTop, box.w, box.h + g.zone - g.zoneTop };

    // Effects: the heading and the row of cards, then the modulation, each a band further down.
    if (box.y >= effectsHeading.y && box.y < modulationHeading.y)
        return box.h > effectsHeading.h ? Box { box.x, box.y + g.zone, box.w, box.h + g.effects } : Box { box.x, box.y + g.zone, box.w, box.h };

    if (box.y >= modulationHeading.y && box.y < voicingKeyboard.y)
        return box.h > modulationHeading.h ? Box { box.x, box.y + g.zone + g.effects, box.w, box.h + g.modulation }
                                           : Box { box.x, box.y + g.zone + g.effects, box.w, box.h };

    if (box.y >= voicingKeyboard.y)
        return { box.x, box.y + g.zone + g.effects + g.modulation, box.w, box.h + g.keyboard };

    return box;
}

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

} // namespace scratchpad::ui::layout
