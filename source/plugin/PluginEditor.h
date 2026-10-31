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

/** One window. Everything inside is laid out on a fixed stage - 1200 x 760 px with the Explain panel showing (the
    sizes are in `ui/Layout.h`) - and the stage is scaled as one unit to fit the window, so no control has to know the
    window's size and nothing sizes itself from its content. Below the top bar sit the signal chain, the effects, the
    modulation and the voicing-and-keyboard card in a column, with the Explain panel to their right.

    Folding the Explain panel to its rail narrows the stage by what the panel gives up (the chain does not change;
    there is nothing for it to grow into) and the window narrows with it at the same scale. The window is resizable
    from 75% to 200% of the stage at its current shape, by its corner in a plugin host and by its frame in the
    standalone app. A host that sets some other size gets the stage fitted and centred inside it. The scale is
    remembered between sessions.

    Below 100% the Explain panel starts folded to a rail. Folding or unfolding it is remembered and then wins over
    the size.

    How the window looks (theme, reduced motion, panel folded, scale) is the user's preference and lives in
    `ViewSettings`, never in the patch or the host session. */
class ScratchpadEditor final : public juce::AudioProcessorEditor,
                               private juce::FocusChangeListener
{
public:
    /** The stage with the Explain panel showing and with it folded to its rail. */
    static constexpr int baseWidth = ui::layout::stageWidth;
    static constexpr int baseHeight = ui::layout::stageHeight;
    static constexpr int foldedWidth = baseWidth - (ui::ExplainPanel::width - ui::ExplainPanel::railWidth);

    /** The smallest and largest scale the window can be dragged to. */
    static constexpr double minScale = 0.75, maxScale = 2.0;

    /** The stage's size now: `foldedWidth` x `baseHeight` while the panel is folded. */
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
    void applyStageSize();
    void refreshExplainState();
    void applyPalette();

    ScratchpadProcessor& processor;

    ui::ViewSettings settings;
    ui::ViewSettings::Theme theme = ui::ViewSettings::Theme::dark;
    double scale = 1.0;
    juce::Point<int> shownStage { baseWidth, baseHeight }; // the stage the window was last fitted to

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
