#pragma once

#include "ui/LookAndFeel.h"
#include "ui/ViewSettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** What the View menu offers. The numbers are the menu's item IDs. */
enum class ViewChoice
{
    none = 0,
    themeDark,
    themeLight,
    reducedMotion, // toggles
    explainPanel,  // toggles
    uiScale75, uiScale100, uiScale125, uiScale150, uiScale175, uiScale200,
    windowSmall, windowMedium, windowLarge
};

/** The UI scale a `uiScale...` choice stands for (0.75 .. 2.0); 0 for any other choice. */
double uiScaleOf (ViewChoice) noexcept;

/** The window preset a `window...` choice stands for: the layout size it opens (1200 x 760, 1440 x 900 or
    1760 x 1000, the three layouts), before the UI scale is applied. A zero size for any other choice. */
juce::Point<int> windowLayoutSizeOf (ViewChoice) noexcept;

/** What the View menu's ticks show. */
struct ViewState
{
    ViewSettings::Theme theme = ViewSettings::Theme::dark;
    bool reducedMotion = false;
    bool explainShown = true;
    double uiScale = 1.0;
};

/** The View menu: Theme (Dark, Light), UI scale (75% to 200%), Window size (Small, Medium, Large), Reduced motion,
    Explain panel. Ticks show the current state. */
juce::PopupMenu buildViewMenu (const ViewState&);

/** Button for the top bar: a circle, half filled, named "View options". It carries no behaviour of its own;
    the window sets `onClick` to show the menu. */
class ViewMenuButton final : public juce::Button
{
public:
    ViewMenuButton();

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
};

} // namespace scratchpad::ui
