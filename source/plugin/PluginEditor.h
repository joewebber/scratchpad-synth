#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/ExplainPanel.h"
#include "ui/LookAndFeel.h"
#include "ui/PatchView.h"
#include "ui/PerformanceBar.h"
#include "ui/TopBar.h"
#include "ui/ViewMenu.h"
#include "ui/ViewSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace scratchpad::plugin
{

/** One window. Everything inside is laid out on a stage in layout pixels, and one scale transform sits over the stage.

    Two separate things decide what you see. The UI scale (a setting, 75% to 200%, in the View menu) says how big a
    knob, a line of text and a button are; dragging the window never changes it. The window's size, divided by the UI
    scale, is the layout size, and `ui::layout::Plan` turns that into every card's box (more of each card, more
    octaves, more connection slots, a taller scope: see `ui/Layout.h` for where extra width and height go). A layout
    smaller than 1200 x 760 (992 x 760 with the Explain panel folded) is not rearranged: the whole stage is scaled down
    to fit, to no less than 75% on the screen. Above 2400 x 1400 the layout stops growing and sits in the middle.

    The window is resizable with no fixed shape, from 900 x 570 to 3200 x 2000 screen pixels (a corner grip in a plugin
    host, the frame in the standalone app), and the View menu's Window size offers Small, Medium and Large (the three
    layouts' sizes at the current UI scale) for hosts that handle resizing badly. The standalone app remembers its
    size; a plugin opens at Small.

    The Explain panel starts folded to a rail. Folding or unfolding it is remembered, and frees width: the chain can
    reach a bigger layout in a smaller window.

    How the window looks (theme, reduced motion, panel folded, UI scale, the standalone's size) is the user's preference
    and lives in `ViewSettings`, never in the patch or the host session. */
class ScratchpadEditor final : public juce::AudioProcessorEditor,
                               private juce::FocusChangeListener
{
public:
    /** Smallest window in screen pixels: the smallest layout at 75%. With the panel folded the layout is narrower. */
    static constexpr double minScale = ui::ViewSettings::minScale;

    explicit ScratchpadEditor (ScratchpadProcessor&);
    ~ScratchpadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    ui::ViewSettings& getViewSettings() noexcept { return settings; }

    /** The user's UI scale setting (0.75 to 2.0). */
    double getUiScale() const noexcept { return uiScale; }

    /** The scale the stage is drawn at: the UI scale, less the shrink that fits a layout under its minimum. */
    double getScale() const noexcept { return scale; }

    /** The layout the stage is in now. */
    const ui::layout::Plan& getPlan() const noexcept { return plan; }

    ui::ViewSettings::Theme getTheme() const noexcept { return theme; }
    bool isExplainCollapsed() const noexcept { return explainPanel.isCollapsed(); }
    ui::ViewState getViewState() const;

    /** The View-menu choices, remembered. */
    void setTheme (ui::ViewSettings::Theme);
    void setReducedMotion (bool);
    void setExplainCollapsed (bool);

    /** Sets the UI scale. The window changes size by the same ratio, so the layout (and what is shown) stays. */
    void setUiScale (double);

    /** Resizes the window to a preset's layout size times the UI scale (limits applied). */
    void setWindowPreset (ui::ViewChoice);

    /** Shows the View menu under its button; a choice is applied with `applyViewChoice`. */
    void showViewMenu();
    void applyViewChoice (ui::ViewChoice);

    /** The window size a layout of `layoutSize` takes at the UI scale, in screen pixels, within the limits. */
    juce::Point<int> windowSizeFor (juce::Point<int> layoutSize) const noexcept;

private:
    /** What the window is laid out on: the layout size, scaled by a transform. */
    class Stage final : public juce::Component
    {
    public:
        Stage() { setTitle ("Controls"); setInterceptsMouseClicks (false, true); }
    };

    /** Writes the standalone's window size to the settings a moment after the last resize, not on every pixel of a drag. */
    class SizeSaver final : private juce::Timer
    {
    public:
        explicit SizeSaver (ScratchpadEditor& o) : owner (o) {}
        ~SizeSaver() override { stopTimer(); }
        void schedule() { startTimer (400); }
        void flush();

    private:
        void timerCallback() override { flush(); }
        ScratchpadEditor& owner;
    };

    void globalFocusChanged (juce::Component*) override;
    void layoutStage();
    void updateConstrainer();
    void applyPalette();
    bool isStandalone() const noexcept { return processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone; }

    ScratchpadProcessor& processor;

    ui::ViewSettings settings;
    ui::ViewSettings::Theme theme = ui::ViewSettings::Theme::dark;
    double uiScale = 1.0, scale = 1.0;
    ui::layout::Plan plan;

    ui::ScratchpadLookAndFeel lookAndFeel { ui::darkPalette() };
    juce::ComponentBoundsConstrainer constrainer;

    Stage stage;
    ui::TopBar topBar;
    ui::PatchView patchView;
    ui::PerformanceBar performanceBar;
    ui::ExplainPanel explainPanel;

    SizeSaver sizeSaver { *this };
};

} // namespace scratchpad::plugin
