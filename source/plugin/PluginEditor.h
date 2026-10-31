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

/** One window. Everything inside is laid out on a stage in logical pixels, and the stage is scaled by a transform so
    no control has to know the window's size. The design is 1200 x 760 px with the Explain panel showing (992 x 760
    folded to its rail); the sizes of the cards are in `ui/Layout.h` and nothing sizes itself from its content.

    The window never scales the stage up: at or above 100% the stage is the size of the window and the extra room is
    used. The Explain panel (or, folded, the space round the column) takes the extra width; the extra height goes
    into the gaps between the signal chain, the effects, the modulation and the keyboard, and into the Explain panel.
    Below 100% the whole stage is scaled down to fit, to a minimum of 75%. So a bigger window means more room, not
    bigger controls.

    The window is resizable (a corner grip in a plugin host, the frame in the standalone app) with no fixed shape. A
    host that sets a size under the minimum gets the stage scaled to fit regardless. The scale below 100% is
    remembered between sessions.

    The Explain panel starts folded to a rail. Folding or unfolding it is remembered.

    How the window looks (theme, reduced motion, panel folded, scale) is the user's preference and lives in
    `ViewSettings`, never in the patch or the host session. */
class ScratchpadEditor final : public juce::AudioProcessorEditor,
                               private juce::FocusChangeListener
{
public:
    /** The design's stage size with the Explain panel showing and with it folded to its rail. */
    static constexpr int baseWidth = ui::layout::stageWidth;
    static constexpr int baseHeight = ui::layout::stageHeight;
    static constexpr int foldedWidth = baseWidth - (ui::ExplainPanel::width - ui::ExplainPanel::railWidth);

    /** The smallest scale the stage is shrunk to; it is never scaled up. */
    static constexpr double minScale = 0.75, maxScale = 1.0;

    /** The design size now: `foldedWidth` x `baseHeight` while the panel is folded. */
    juce::Point<int> getBaseSize() const noexcept;

    /** The stage's size in its own pixels: the window's size divided by the scale, never less than `getBaseSize()`. */
    juce::Point<int> getStageSize() const noexcept;

    explicit ScratchpadEditor (ScratchpadProcessor&);
    ~ScratchpadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    ui::ViewSettings& getViewSettings() noexcept { return settings; }

    /** The stage's scale: 1.0 is 1500 x 900 on screen. */
    double getScale() const noexcept { return scale; }

    ui::ViewSettings::Theme getTheme() const noexcept { return theme; }
    bool isExplainCollapsed() const noexcept { return explainPanel.isCollapsed(); }
    ui::ViewState getViewState() const;

    /** The three View-menu choices, remembered. */
    void setTheme (ui::ViewSettings::Theme);
    void setReducedMotion (bool);
    void setExplainCollapsed (bool);

    /** Shows the View menu under its button; a choice is applied with `applyViewChoice`. */
    void showViewMenu();
    void applyViewChoice (ui::ViewChoice);

private:
    /** What the window is laid out on: `getStageSize()`, scaled by a transform. */
    class Stage final : public juce::Component
    {
    public:
        Stage() { setTitle ("Controls"); setInterceptsMouseClicks (false, true); }
    };

    /** Writes the scale to the settings a moment after the last resize, not on every pixel of a drag. */
    class ScaleSaver final : private juce::Timer
    {
    public:
        explicit ScaleSaver (ScratchpadEditor& o) : owner (o) {}
        ~ScaleSaver() override { stopTimer(); }
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

    ScratchpadProcessor& processor;

    ui::ViewSettings settings;
    ui::ViewSettings::Theme theme = ui::ViewSettings::Theme::dark;
    double scale = 1.0;

    ui::ScratchpadLookAndFeel lookAndFeel { ui::darkPalette() };
    juce::ComponentBoundsConstrainer constrainer;

    Stage stage;
    ui::TopBar topBar;
    ui::PatchView patchView;
    ui::PerformanceBar performanceBar;
    ui::ExplainPanel explainPanel;

    ScaleSaver scaleSaver { *this };
};

} // namespace scratchpad::plugin
