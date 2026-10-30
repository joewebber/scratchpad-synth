#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "ui/Motion.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <optional>
#include <vector>

using namespace scratchpad;
using Catch::Matchers::WithinAbs;
using plugin::ScratchpadEditor;
using ui::ViewSettings;

namespace
{
    /** Every test starts from, and leaves behind, the default preferences. */
    struct CleanPreferences
    {
        CleanPreferences() { reset(); }
        ~CleanPreferences() { reset(); }

        static void reset()
        {
            ViewSettings().getFile().deleteFile();
            ui::motion::setReduced (false);
        }
    };

    void collect (juce::Component& c, std::vector<juce::Component*>& out)
    {
        out.push_back (&c);

        for (auto* child : c.getChildren())
            collect (*child, out);
    }

    template <typename T>
    std::vector<T*> all (juce::Component& root)
    {
        std::vector<juce::Component*> everything;
        collect (root, everything);

        std::vector<T*> found;
        for (auto* c : everything)
            if (auto* t = dynamic_cast<T*> (c))
                found.push_back (t);

        return found;
    }

    template <typename T>
    T& only (juce::Component& root)
    {
        auto found = all<T> (root);
        REQUIRE (found.size() == 1);
        return *found.front();
    }

    juce::Button& buttonTitled (juce::Component& root, const juce::String& title)
    {
        for (auto* b : all<juce::Button> (root))
            if (b->getTitle() == title)
                return *b;

        FAIL ("no button titled " << title.toStdString());
        return *all<juce::Button> (root).front();
    }

    void setSizeFor (ScratchpadEditor& e, int percent)
    {
        e.setSize (1500 * percent / 100, 900 * percent / 100);
    }

    /** The ticked state of the menu item with this text, looking inside sub-menus. */
    std::optional<bool> tickOf (const juce::PopupMenu& menu, const juce::String& text)
    {
        juce::PopupMenu::MenuItemIterator it (menu, true);

        while (it.next())
            if (it.getItem().text == text)
                return it.getItem().isTicked;

        return std::nullopt;
    }

    int idOf (const juce::PopupMenu& menu, const juce::String& text)
    {
        juce::PopupMenu::MenuItemIterator it (menu, true);

        while (it.next())
            if (it.getItem().text == text)
                return it.getItem().itemID;

        return -1;
    }

    bool within (juce::Rectangle<int> inner, juce::Rectangle<int> outer)
    {
        return outer.expanded (1).contains (inner);
    }
}

TEST_CASE ("The editor opens at 100% with the Explain panel showing", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    CHECK (e.getWidth() == 1500);
    CHECK (e.getHeight() == 900);
    CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
    CHECK_FALSE (e.isExplainCollapsed());
    CHECK (e.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (ui::motion::isReduced());
}

TEST_CASE ("The window resizes between 75% and 200% at one shape", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    REQUIRE (e.isResizable());
    auto* constrainer = e.getConstrainer();
    REQUIRE (constrainer != nullptr);
    CHECK (constrainer->getMinimumWidth() == 1125);
    CHECK (constrainer->getMinimumHeight() == 675);
    CHECK (constrainer->getMaximumWidth() == 3000);
    CHECK (constrainer->getMaximumHeight() == 1800);
    CHECK_THAT (constrainer->getFixedAspectRatio(), WithinAbs (1500.0 / 900.0, 1.0e-9));

    const auto asked = [&e] (juce::Rectangle<int> bounds)
    {
        e.setBoundsConstrained (bounds);
        return e.getBounds();
    };

    SECTION ("too big is held at 200%")
    {
        const auto b = asked ({ 0, 0, 5000, 4000 });
        CHECK (b.getWidth() <= 3000);
        CHECK (b.getHeight() <= 1800);
        CHECK_THAT ((double) b.getWidth() / (double) b.getHeight(), WithinAbs (1500.0 / 900.0, 0.002));
    }

    SECTION ("too small is held at 75%")
    {
        const auto b = asked ({ 0, 0, 300, 200 });
        CHECK (b.getWidth() >= 1125);
        CHECK (b.getHeight() >= 675);
        CHECK_THAT ((double) b.getWidth() / (double) b.getHeight(), WithinAbs (1500.0 / 900.0, 0.002));
    }

    SECTION ("a size in range keeps its shape")
    {
        const auto b = asked ({ 0, 0, 1800, 1000 });
        CHECK (b.getWidth() >= 1125);
        CHECK (b.getWidth() <= 3000);
        CHECK_THAT ((double) b.getWidth() / (double) b.getHeight(), WithinAbs (1500.0 / 900.0, 0.002));
    }
}

TEST_CASE ("The Explain panel folds below 100% and opens at 100% unless the user has chosen", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& panel = only<ui::ExplainPanel> (e);

    setSizeFor (e, 75);
    CHECK_THAT (e.getScale(), WithinAbs (0.75, 1.0e-9));
    CHECK (e.isExplainCollapsed());
    CHECK (panel.getWidth() == ui::ExplainPanel::railWidth);

    setSizeFor (e, 93);
    CHECK (e.isExplainCollapsed());

    setSizeFor (e, 100);
    CHECK_FALSE (e.isExplainCollapsed());
    CHECK (panel.getWidth() == ui::ExplainPanel::width);

    setSizeFor (e, 200);
    CHECK_THAT (e.getScale(), WithinAbs (2.0, 1.0e-9));
    CHECK_FALSE (e.isExplainCollapsed());

    // 1499 x 899 is 100% for all anyone can tell.
    e.setSize (1499, 899);
    CHECK_FALSE (e.isExplainCollapsed());
}

TEST_CASE ("The user's choice about the Explain panel beats the window size and is remembered", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;

    {
        ScratchpadEditor e (p);
        setSizeFor (e, 75);
        REQUIRE (e.isExplainCollapsed());

        e.setExplainCollapsed (false);
        CHECK_FALSE (e.isExplainCollapsed());

        setSizeFor (e, 90);
        CHECK_FALSE (e.isExplainCollapsed()); // the size no longer decides

        setSizeFor (e, 100);
        e.setExplainCollapsed (true);
        CHECK (e.isExplainCollapsed());
        setSizeFor (e, 150);
        CHECK (e.isExplainCollapsed());
    }

    ScratchpadEditor again (p); // a later session
    CHECK (again.isExplainCollapsed());
    CHECK (only<ui::ExplainPanel> (again).getWidth() == ui::ExplainPanel::railWidth);
}

TEST_CASE ("The fold and unfold buttons, and the panel's own parts, match its state", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& panel = only<ui::ExplainPanel> (e);
    auto& fold = buttonTitled (e, "Fold Explain panel");
    auto& unfold = buttonTitled (e, "Show Explain panel");
    auto& scope = only<ui::Scope> (e);

    SECTION ("showing")
    {
        CHECK (fold.isVisible());
        CHECK_FALSE (unfold.isVisible());
        CHECK (scope.isVisible());
        CHECK (scope.getLiveIntervalMs() > 0);

        fold.onClick(); // `triggerClick` is asynchronous
        CHECK (panel.isCollapsed());
        CHECK (e.isExplainCollapsed());
    }

    SECTION ("folded")
    {
        e.setExplainCollapsed (true);
        CHECK_FALSE (fold.isVisible());
        CHECK (unfold.isVisible());
        CHECK (unfold.getBounds().getWidth() > 0);
        CHECK (within (unfold.getBounds(), panel.getLocalBounds()));

        // Nobody can see the scope, so it must not keep running.
        CHECK_FALSE (scope.isVisible());
        CHECK (scope.getLiveIntervalMs() == 0);

        unfold.onClick();
        CHECK_FALSE (panel.isCollapsed());
        CHECK (scope.getLiveIntervalMs() > 0);
    }

    CHECK (ui::ExplainPanel::explainIdOf (&fold) == "control.explainToggle");
    CHECK (ui::ExplainPanel::explainIdOf (&unfold) == "control.explainToggle");
}

TEST_CASE ("A module's ? button unfolds a folded Explain panel and pins the module", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    p.addModule (plugin::Module::filter);
    ScratchpadEditor e (p);
    auto& panel = only<ui::ExplainPanel> (e);

    setSizeFor (e, 75);
    REQUIRE (panel.isCollapsed());
    REQUIRE (panel.getShownId() == "osc1");

    juce::Button* filterHelp = nullptr;
    for (auto* b : all<juce::Button> (e))
        if (b->getTitle().startsWith ("Explain ") && b->getProperties()["explainId"].toString() == "filter")
            filterHelp = b;

    REQUIRE (filterHelp != nullptr);
    filterHelp->onClick();

    CHECK_FALSE (panel.isCollapsed());
    CHECK (panel.getShownId() == "filter");
}

TEST_CASE ("The signal chain gets the room the Explain panel gives up", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    p.addModule (plugin::Module::filter);
    p.addModule (plugin::Module::lfo1);
    ScratchpadEditor e (p);
    auto& view = only<ui::PatchView> (e);
    auto& panel = only<ui::ExplainPanel> (e);

    const int expandedWidth = view.getWidth();
    e.setExplainCollapsed (true);
    const int foldedWidth = view.getWidth();

    CHECK (foldedWidth == expandedWidth + (ui::ExplainPanel::width - ui::ExplainPanel::railWidth));

    // Nothing collides: the chain ends where the rail starts, with the usual gap, and nothing sticks out of the chain.
    CHECK (view.getRight() + 10 == panel.getX());
    CHECK (panel.getRight() == ScratchpadEditor::baseWidth - 12);

    for (auto* child : view.getChildren())
        if (child->isVisible())
        {
            INFO (child->getTitle().toStdString() << " " << child->getBounds().toString().toStdString());
            CHECK (within (child->getBounds(), view.getLocalBounds()));
        }

    e.setExplainCollapsed (false);
    CHECK (view.getWidth() == expandedWidth);
}

TEST_CASE ("Controls are where the eye sees them at every scale", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    e.setVisible (true); // hit-testing only finds what is visible
    auto& button = buttonTitled (e, "New patch");

    for (int percent : { 75, 100, 133, 200 })
    {
        INFO (percent << "%");
        setSizeFor (e, percent);

        const double scale = percent / 100.0;
        const auto centre = button.getLocalBounds().getCentre();

        // Where the button's centre falls in the window: its place on the 1500 x 900 stage, scaled.
        const auto inEditor = e.getLocalPoint (&button, centre);
        const auto onStage = button.getParentComponent()->getLocalPoint (&button, centre);
        const auto topBarOrigin = button.getParentComponent()->getParentComponent()->getLocalPoint (button.getParentComponent(), juce::Point<int> (0, 0));

        CHECK_THAT ((double) inEditor.x, WithinAbs ((double) (onStage.x + topBarOrigin.x) * scale, 1.5));
        CHECK_THAT ((double) inEditor.y, WithinAbs ((double) (onStage.y + topBarOrigin.y) * scale, 1.5));

        // The pointer finds the button there, and the point maps back to the middle of it.
        CHECK (e.getComponentAt (inEditor) == &button);
        const auto back = button.getLocalPoint (&e, inEditor);
        CHECK (std::abs (back.x - centre.x) <= 1);
        CHECK (std::abs (back.y - centre.y) <= 1);

        // A point just outside the button's edge, in window pixels, is not the button.
        const auto corner = e.getLocalArea (&button, button.getLocalBounds()).getBottomRight();
        CHECK (e.getComponentAt (corner + juce::Point<int> (3, 3)) != &button);
    }
}

TEST_CASE ("A host that sets an odd size gets the window fitted and centred", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& top = only<ui::TopBar> (e);

    SECTION ("wider than the shape: the sides are bare")
    {
        e.setSize (2000, 900); // fits at 100%, 250 px spare each side
        CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
        const auto area = e.getLocalArea (&top, top.getLocalBounds());
        CHECK (area.getX() == 250 + 12);
        CHECK (area.getY() == 12);
    }

    SECTION ("taller than the shape: top and bottom are bare")
    {
        e.setSize (1500, 1100);
        CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
        const auto area = e.getLocalArea (&top, top.getLocalBounds());
        CHECK (area.getY() == 100 + 12);
    }

    SECTION ("narrower than the minimum: everything still fits")
    {
        e.setSize (1000, 800);
        CHECK_THAT (e.getScale(), WithinAbs (1000.0 / 1500.0, 1.0e-9));
        CHECK (within (e.getLocalArea (&top, top.getLocalBounds()), e.getLocalBounds()));
    }
}

TEST_CASE ("The scale is remembered between sessions", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;

    {
        ScratchpadEditor e (p);
        setSizeFor (e, 160);
    } // the scale is written when the editor closes at the latest

    {
        ScratchpadEditor again (p);
        CHECK (again.getWidth() == 2400);
        CHECK (again.getHeight() == 1440);
        CHECK_THAT (again.getScale(), WithinAbs (1.6, 1.0e-9));
        setSizeFor (again, 75);
    }

    ScratchpadEditor third (p);
    CHECK (third.getWidth() == 1125);
    CHECK (third.getHeight() == 675);
    CHECK (third.isExplainCollapsed());
}

TEST_CASE ("A damaged preferences file still opens the editor at the defaults", "[view][editor]")
{
    CleanPreferences clean;
    const auto file = ViewSettings().getFile();
    file.getParentDirectory().createDirectory();
    REQUIRE (file.replaceWithText ("<PROPERTIES><VALUE name=\"editorScale\" val=\"banana\"/><VALUE name=\"theme\" val=\"neon"));

    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    CHECK (e.getWidth() == 1500);
    CHECK (e.getTheme() == ViewSettings::Theme::dark);
}

TEST_CASE ("The View menu shows the current state with ticks", "[view][menu]")
{
    const auto dark = ui::buildViewMenu ({ ViewSettings::Theme::dark, false, true });
    CHECK (tickOf (dark, "Dark") == true);
    CHECK (tickOf (dark, "Light") == false);
    CHECK (tickOf (dark, "Reduced motion") == false);
    CHECK (tickOf (dark, "Explain panel") == true);

    const auto light = ui::buildViewMenu ({ ViewSettings::Theme::light, true, false });
    CHECK (tickOf (light, "Dark") == false);
    CHECK (tickOf (light, "Light") == true);
    CHECK (tickOf (light, "Reduced motion") == true);
    CHECK (tickOf (light, "Explain panel") == false);

    CHECK (idOf (dark, "Dark") == (int) ui::ViewChoice::themeDark);
    CHECK (idOf (dark, "Light") == (int) ui::ViewChoice::themeLight);
    CHECK (idOf (dark, "Reduced motion") == (int) ui::ViewChoice::reducedMotion);
    CHECK (idOf (dark, "Explain panel") == (int) ui::ViewChoice::explainPanel);

    // Theme is a sub-menu.
    juce::PopupMenu::MenuItemIterator top (dark, false);
    bool hasThemeSubMenu = false;
    while (top.next())
        hasThemeSubMenu = hasThemeSubMenu || (top.getItem().text == "Theme" && top.getItem().subMenu != nullptr);
    CHECK (hasThemeSubMenu);
}

TEST_CASE ("Choosing from the View menu changes the window and is remembered", "[view][menu]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;

    {
        ScratchpadEditor e (p);
        const auto state = e.getViewState();
        CHECK (state.theme == ViewSettings::Theme::dark);
        CHECK_FALSE (state.reducedMotion);
        CHECK (state.explainShown);

        e.applyViewChoice (ui::ViewChoice::themeLight);
        CHECK (e.getTheme() == ViewSettings::Theme::light);

        e.applyViewChoice (ui::ViewChoice::reducedMotion);
        CHECK (ui::motion::isReduced());
        CHECK (e.getViewState().reducedMotion);

        e.applyViewChoice (ui::ViewChoice::explainPanel); // shown -> folded
        CHECK (e.isExplainCollapsed());
        CHECK_FALSE (e.getViewState().explainShown);

        e.applyViewChoice (ui::ViewChoice::none);
        CHECK (e.getTheme() == ViewSettings::Theme::light);
    }

    // A later session: all three come back.
    ui::motion::setReduced (false);
    ScratchpadEditor again (p);
    CHECK (again.getTheme() == ViewSettings::Theme::light);
    CHECK (ui::motion::isReduced());
    CHECK (again.isExplainCollapsed());
    CHECK (&ui::paletteOf (again) == &ui::lightPalette());

    again.applyViewChoice (ui::ViewChoice::themeDark);
    again.applyViewChoice (ui::ViewChoice::reducedMotion); // toggles back off
    again.applyViewChoice (ui::ViewChoice::explainPanel);  // folded -> shown
    CHECK (again.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (ui::motion::isReduced());
    CHECK_FALSE (again.isExplainCollapsed());
}

TEST_CASE ("The view button is in the top bar, named, and explained", "[view][menu]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& top = only<ui::TopBar> (e);
    auto& view = only<ui::ViewMenuButton> (e);

    CHECK (view.getTitle() == "View options");
    CHECK (ui::ExplainPanel::explainIdOf (&view) == "control.viewMenu");
    CHECK (view.isVisible());
    CHECK (within (view.getBounds(), top.getLocalBounds()));

    // It does not sit on anything else in the bar.
    for (auto* child : top.getChildren())
        if (child != &view && child->isVisible())
        {
            INFO (child->getTitle().toStdString());
            CHECK_FALSE (child->getBounds().intersects (view.getBounds()));
        }
}

TEST_CASE ("Switching theme reaches every part of the window", "[view][theme]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;

    // Everything that can show, so every card's own colours are in play.
    for (auto m : plugin::allModules)
        p.addModule (m);

    ScratchpadEditor e (p);

    const auto isOneOf = [] (juce::Colour c, const ui::Palette& pal)
    {
        for (auto ink : { pal.srcInk, pal.fltInk, pal.ampInk, pal.modInk, pal.fxInk, pal.perfInk })
            if (ink == c)
                return true;

        return false;
    };

    const auto sliders = all<juce::Slider> (e);
    REQUIRE (sliders.size() > 20);

    for (auto* s : sliders)
        CHECK (isOneOf (s->findColour (juce::Slider::rotarySliderFillColourId), ui::darkPalette()));

    e.setTheme (ViewSettings::Theme::light);
    CHECK (&ui::paletteOf (e) == &ui::lightPalette());

    // Knob arcs are coloured by cards that set them themselves: none may have kept the dark theme's.
    int stale = 0;
    for (auto* s : sliders)
        if (! isOneOf (s->findColour (juce::Slider::rotarySliderFillColourId), ui::lightPalette()))
        {
            ++stale;
            INFO (s->getTitle().toStdString());
        }

    CHECK (stale == 0);

    // And every component asks the same look-and-feel, so every paint reads the light palette.
    for (auto* c : all<juce::Component> (e))
        CHECK (&ui::paletteOf (*c) == &ui::lightPalette());

    e.setTheme (ViewSettings::Theme::dark);

    for (auto* s : sliders)
        CHECK (isOneOf (s->findColour (juce::Slider::rotarySliderFillColourId), ui::darkPalette()));
}

TEST_CASE ("Each theme paints its own background, across the whole window", "[view][theme]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    for (int percent : { 75, 100, 200 })
        for (auto theme : { ViewSettings::Theme::dark, ViewSettings::Theme::light })
        {
            const auto& palette = theme == ViewSettings::Theme::dark ? ui::darkPalette() : ui::lightPalette();
            INFO (percent << "% " << (theme == ViewSettings::Theme::dark ? "dark" : "light"));

            e.setTheme (theme);
            setSizeFor (e, percent);
            const auto image = e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f);

            REQUIRE (image.getWidth() == e.getWidth());
            CHECK (image.getPixelAt (2, 2).withAlpha (1.0f) == palette.bg);
            CHECK (image.getPixelAt (e.getWidth() - 3, 2).withAlpha (1.0f) == palette.bg);

            // The rest of the window is not blank: panels are painted over the background.
            int different = 0;
            for (int y = 0; y < image.getHeight(); y += 7)
                for (int x = 0; x < image.getWidth(); x += 7)
                    different += image.getPixelAt (x, y).withAlpha (1.0f) != palette.bg ? 1 : 0;

            CHECK (different > (image.getWidth() / 7) * (image.getHeight() / 7) / 3);
        }
}
