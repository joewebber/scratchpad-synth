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

/** One window. Everything inside is laid out on a fixed 1500 x 900 stage (the design's 1200 x 760 plus room for
    the Explain panel, the wider cards and the row of effects under the signal chain) and the stage is scaled to
    fit the window, so no control has to know the window's size. Below the top bar sit the signal chain over the
    keyboard, with the Explain panel to their right.

    The window is resizable from 75% (1125 x 675) to 200% (3000 x 1800) at a fixed aspect ratio, by its corner in a
    plugin host and by its frame in the standalone app. A host that sets some other size gets the stage fitted
    and centred inside it. The scale is remembered between sessions.

    Below 100% the Explain panel starts folded to a rail, so the signal chain keeps its room. Folding or
    unfolding it is remembered and then wins over the size.

    How the window looks (theme, reduced motion, panel folded, scale) is the user's preference and lives in
    `ViewSettings`, never in the patch or the host session. */
class ScratchpadEditor final : public juce::AudioProcessorEditor,
                               private juce::FocusChangeListener
{
public:
    static constexpr int baseWidth = 1500;
    static constexpr int baseHeight = 900;

    /** The smallest and largest sizes the window can be dragged to. */
    static constexpr int minWidth = 1125, minHeight = 675;
    static constexpr int maxWidth = 3000, maxHeight = 1800;

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
    /** What the window is laid out on: always `baseWidth` x `baseHeight`, scaled by a transform. */
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
    void refreshExplainState();
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
