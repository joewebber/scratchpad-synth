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
    explainPanel   // toggles
};

/** What the View menu's ticks show. */
struct ViewState
{
    ViewSettings::Theme theme = ViewSettings::Theme::dark;
    bool reducedMotion = false;
    bool explainShown = true;
};

/** The View menu: Theme (Dark, Light), Reduced motion, Explain panel. Ticks show the current state. */
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
