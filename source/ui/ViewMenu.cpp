#include "ui/ViewMenu.h"

namespace scratchpad::ui
{

juce::PopupMenu buildViewMenu (const ViewState& state)
{
    juce::PopupMenu theme;
    theme.addItem ((int) ViewChoice::themeDark, "Dark", true, state.theme == ViewSettings::Theme::dark);
    theme.addItem ((int) ViewChoice::themeLight, "Light", true, state.theme == ViewSettings::Theme::light);

    juce::PopupMenu menu;
    menu.addSubMenu ("Theme", theme);
    menu.addItem ((int) ViewChoice::reducedMotion, "Reduced motion", true, state.reducedMotion);
    menu.addItem ((int) ViewChoice::explainPanel, "Explain panel", true, state.explainShown);
    return menu;
}

ViewMenuButton::ViewMenuButton()
    : juce::Button ("View options")
{
    setTitle ("View options");
    setDescription ("Theme, reduced motion and the Explain panel");
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
