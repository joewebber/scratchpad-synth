#include "ui/ViewMenu.h"

namespace scratchpad::ui
{

namespace
{
    constexpr ViewChoice scaleChoices[] = { ViewChoice::uiScale75, ViewChoice::uiScale100, ViewChoice::uiScale125,
                                            ViewChoice::uiScale150, ViewChoice::uiScale175, ViewChoice::uiScale200 };
}

double uiScaleOf (ViewChoice choice) noexcept
{
    switch (choice)
    {
        case ViewChoice::uiScale75:   return 0.75;
        case ViewChoice::uiScale100:  return 1.0;
        case ViewChoice::uiScale125:  return 1.25;
        case ViewChoice::uiScale150:  return 1.5;
        case ViewChoice::uiScale175:  return 1.75;
        case ViewChoice::uiScale200:  return 2.0;
        default:                      return 0.0;
    }
}

juce::Point<int> windowLayoutSizeOf (ViewChoice choice) noexcept
{
    switch (choice)
    {
        case ViewChoice::windowSmall:   return { 1200, 760 };
        case ViewChoice::windowMedium:  return { 1440, 900 };
        case ViewChoice::windowLarge:   return { 1760, 1000 };
        default:                        return { 0, 0 };
    }
}

juce::PopupMenu buildViewMenu (const ViewState& state)
{
    juce::PopupMenu theme;
    theme.addItem ((int) ViewChoice::themeDark, "Dark", true, state.theme == ViewSettings::Theme::dark);
    theme.addItem ((int) ViewChoice::themeLight, "Light", true, state.theme == ViewSettings::Theme::light);

    juce::PopupMenu scale;
    for (const auto choice : scaleChoices)
        scale.addItem ((int) choice, juce::String (juce::roundToInt (uiScaleOf (choice) * 100.0)) + "%", true,
                       std::abs (state.uiScale - uiScaleOf (choice)) < 0.005);

    juce::PopupMenu size;
    size.addItem ((int) ViewChoice::windowSmall, "Small");
    size.addItem ((int) ViewChoice::windowMedium, "Medium");
    size.addItem ((int) ViewChoice::windowLarge, "Large");

    juce::PopupMenu menu;
    menu.addSubMenu ("Theme", theme);
    menu.addSubMenu ("UI scale", scale);
    menu.addSubMenu ("Window size", size);
    menu.addItem ((int) ViewChoice::reducedMotion, "Reduced motion", true, state.reducedMotion);
    menu.addItem ((int) ViewChoice::explainPanel, "Explain panel", true, state.explainShown);
    return menu;
}

ViewMenuButton::ViewMenuButton()
    : juce::Button ("View options")
{
    setTitle ("View options");
    setDescription ("Theme, UI scale, window size, reduced motion and the Explain panel");
    setPaintingIsUnclipped (true);
    getProperties().set ("explainId", "control.viewMenu");
}

void ViewMenuButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);

    // A circle with its right half filled: light and dark.
    constexpr float radius = 7.0f;
    const auto centre = getLocalBounds().toFloat().getCentre();

    g.setColour (paletteOf (*this).ink);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.4f);

    juce::Path half;
    half.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::pi, true);
    half.closeSubPath();
    g.fillPath (half);
}

} // namespace scratchpad::ui
