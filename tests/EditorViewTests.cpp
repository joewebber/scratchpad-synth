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

    /** A window of `percent` of the smallest layout's size, in screen pixels. */
    void setSizeFor (ScratchpadEditor& e, int percent)
    {
        e.setSize (ui::layout::minLayoutWidth * percent / 100, ui::layout::minLayoutHeight * percent / 100);
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

    /** The panel starts folded; a test that wants it showing says so, as a user does. */
    void showPanel() { ViewSettings().setExplainCollapsed (false); }

    bool within (juce::Rectangle<int> inner, juce::Rectangle<int> outer)
    {
        return outer.expanded (1).contains (inner);
    }
}

TEST_CASE ("The editor opens at the smallest layout, 100%, with the Explain panel folded", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    CHECK (e.getWidth() == 1200);
    CHECK (e.getHeight() == 760);
    CHECK_THAT (e.getUiScale(), WithinAbs (1.0, 1.0e-9));
    CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
    CHECK (e.isExplainCollapsed());
    CHECK (e.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (ui::motion::isReduced());
}

TEST_CASE ("Dragging the window changes the layout, never the size of a control", "[view][editor]")
{
    CleanPreferences clean;
    showPanel();
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    REQUIRE (e.isResizable());
    auto* constrainer = e.getConstrainer();
    REQUIRE (constrainer != nullptr);
    CHECK (constrainer->getMinimumWidth() == 900);
    CHECK (constrainer->getMinimumHeight() == 570);
    CHECK (constrainer->getMaximumWidth() == 3200);
    CHECK (constrainer->getMaximumHeight() == 2000);
    CHECK_THAT (constrainer->getFixedAspectRatio(), WithinAbs (0.0, 1.0e-9)); // any shape

    const auto asked = [&e] (juce::Rectangle<int> bounds)
    {
        e.setBoundsConstrained (bounds);
        return e.getBounds();
    };

    SECTION ("too small is held at 75%")
    {
        const auto b = asked ({ 0, 0, 300, 200 });
        CHECK (b.getWidth() == 900);
        CHECK (b.getHeight() == 570);
        CHECK_THAT (e.getScale(), WithinAbs (0.75, 1.0e-9));
    }

    SECTION ("under the smallest layout the whole stage shrinks and nothing rearranges")
    {
        const auto before = e.getPlan();
        asked ({ 0, 0, 1000, 700 });
        CHECK_THAT (e.getScale(), WithinAbs (1000.0 / 1200.0, 1.0e-9));
        CHECK (e.getPlan().filter.w == before.filter.w);
        CHECK (e.getPlan().columnWidth == before.columnWidth);
        CHECK (e.getPlan().width >= ui::layout::minLayoutWidth);
    }

    SECTION ("bigger is more layout, not bigger controls")
    {
        const auto b = asked ({ 0, 0, 1760, 1000 });
        CHECK (b.getWidth() == 1760);
        CHECK (b.getHeight() == 1000);
        CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
        CHECK (e.getPlan().width == 1760);
        CHECK (e.getPlan().height == 1000);
        CHECK (e.getPlan().filter.w == 400);
        CHECK (only<ui::PatchView> (e).getWidth() == e.getPlan().columnWidth);
    }

    SECTION ("past 3200 x 2000 the host's window is held")
    {
        const auto b = asked ({ 0, 0, 5000, 4000 });
        CHECK (b.getWidth() == 3200);
        CHECK (b.getHeight() == 2000);
    }
}

TEST_CASE ("The Explain panel starts folded whatever the window size", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& panel = only<ui::ExplainPanel> (e);

    for (int percent : { 75, 100, 200 })
    {
        setSizeFor (e, percent);
        INFO (percent << "%");
        CHECK (e.isExplainCollapsed());
        CHECK (panel.getWidth() == ui::ExplainPanel::railWidth);
    }
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
    showPanel();
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

TEST_CASE ("Folding the Explain panel frees its width for the chain", "[view][editor]")
{
    CleanPreferences clean;
    showPanel();
    plugin::ScratchpadProcessor p;
    p.addModule (plugin::Module::filter);
    p.addModule (plugin::Module::lfo1);
    ScratchpadEditor e (p);
    e.setSize (1300, 760);
    auto& view = only<ui::PatchView> (e);
    auto& panel = only<ui::ExplainPanel> (e);

    const auto chain = view.getBounds();
    const auto size = e.getBounds();
    const auto filterBefore = e.getPlan().filter.w;
    CHECK (panel.getRight() == e.getPlan().width - 12);

    e.setExplainCollapsed (true);

    // The window stays as it is. The panel is a rail, and the chain takes what it left, so its cards are wider.
    CHECK (e.getBounds() == size);
    CHECK (panel.getWidth() == ui::ExplainPanel::railWidth);
    CHECK (view.getX() == chain.getX());
    CHECK (view.getWidth() == chain.getWidth() + (ui::layout::explainWidthCompact - ui::layout::railWidth));
    CHECK (e.getPlan().filter.w > filterBefore);
    CHECK (view.getRight() < panel.getX());
    CHECK (panel.getRight() == e.getPlan().width - 12);

    for (auto* child : view.getChildren())
        if (child->isVisible())
        {
            INFO (child->getTitle().toStdString() << " " << child->getBounds().toString().toStdString());
            CHECK (within (child->getBounds(), view.getLocalBounds()));
        }

    e.setExplainCollapsed (false);
    CHECK (view.getBounds() == chain);
    CHECK (panel.getRight() == e.getPlan().width - 12);
}

TEST_CASE ("Controls are where the eye sees them at every scale", "[view][editor]")
{
    CleanPreferences clean;
    showPanel();
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    e.setVisible (true); // hit-testing only finds what is visible
    auto& button = buttonTitled (e, "New patch");

    for (int percent : { 75, 100, 133, 200 })
    {
        INFO (percent << "%");
        setSizeFor (e, percent);

        const double scale = e.getScale();
        const auto centre = button.getLocalBounds().getCentre();

        // Where the button's centre falls in the window: its place on the stage, scaled.
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

TEST_CASE ("The layout follows the window's width and height separately", "[view][editor]")
{
    CleanPreferences clean;
    showPanel();
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    auto& top = only<ui::TopBar> (e);
    auto& panel = only<ui::ExplainPanel> (e);
    auto& keys = only<ui::PerformanceBar> (e);

    SECTION ("wider: the chain takes the width first, then the panel")
    {
        e.setSize (1300, 760);
        CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
        CHECK (e.getLocalArea (&top, top.getLocalBounds()).getRight() == e.getWidth() - 12);
        CHECK (panel.getWidth() == ui::ExplainPanel::width);
        CHECK (only<ui::PatchView> (e).getWidth() == 1300 - 32 - ui::ExplainPanel::width);
        CHECK (panel.getRight() == e.getWidth() - 12);

        e.setSize (1440, 760);
        CHECK (panel.getWidth() == 300);
    }

    SECTION ("taller: the extra goes to the cards and the panel, not the gaps")
    {
        e.setSize (1200, 1060);
        CHECK_THAT (e.getScale(), WithinAbs (1.0, 1.0e-9));
        CHECK (panel.getHeight() == ui::layout::explain.h + 300);
        CHECK (keys.getHeight() == ui::layout::voicingKeyboard.h + ui::layout::maxKeyHeightGrowth);
        CHECK (keys.getBottom() == e.getHeight() - 12);
    }

    SECTION ("a wide, short window gets the width and keeps the minimum height")
    {
        e.setSize (1600, 760);
        CHECK (keys.getHeight() == ui::layout::voicingKeyboard.h);
        CHECK (e.getPlan().filter.h == ui::layout::filter.h);
        CHECK (e.getPlan().filter.w > ui::layout::filter.w);
    }

    SECTION ("smaller than the smallest layout: everything is scaled down to fit")
    {
        e.setSize (900, 700);
        CHECK_THAT (e.getScale(), WithinAbs (0.75, 1.0e-9)); // never below 75%; the host's window clips what is left
        CHECK (e.getLocalArea (&top, top.getLocalBounds()).getX() >= 0);
    }

    SECTION ("past 2400 x 1400 the layout is centred, and the window's background shows round it")
    {
        e.setSize (3000, 1800);
        CHECK (e.getPlan().originX == 300);
        CHECK (e.getLocalArea (&top, top.getLocalBounds()).getX() == 300 + 12);
        CHECK (panel.getRight() == 3000 - 300 - 12);
    }
}

TEST_CASE ("The UI scale is a setting: it sizes the controls, the window does not", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;

    {
        ScratchpadEditor e (p);
        e.setSize (1440, 900);
        const auto plan = e.getPlan();

        e.setUiScale (1.5);
        CHECK_THAT (e.getUiScale(), WithinAbs (1.5, 1.0e-9));
        CHECK_THAT (e.getScale(), WithinAbs (1.5, 1.0e-9));
        CHECK (e.getWidth() == 2160); // the window follows, so the layout is what it was
        CHECK (e.getHeight() == 1350);
        CHECK (e.getPlan().columnWidth == plan.columnWidth);
        CHECK (e.getPlan().filter.w == plan.filter.w);

        // Dragging the window changes the layout, not the scale.
        e.setSize (2640, 1500);
        CHECK_THAT (e.getScale(), WithinAbs (1.5, 1.0e-9));
        CHECK (e.getPlan().width == 1760);
        CHECK (e.getPlan().height == 1000);

        e.applyViewChoice (ui::ViewChoice::uiScale200);
        CHECK_THAT (e.getUiScale(), WithinAbs (2.0, 1.0e-9));
    }

    ScratchpadEditor again (p); // a later session
    CHECK_THAT (again.getUiScale(), WithinAbs (2.0, 1.0e-9));
    CHECK_THAT (again.getScale(), WithinAbs (2.0, 1.0e-9));
    CHECK (again.getWidth() == 2400);
    CHECK (again.getHeight() == 1520);
}

TEST_CASE ("The window size presets open the three layouts at the UI scale", "[view][editor]")
{
    CleanPreferences clean;
    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);

    e.applyViewChoice (ui::ViewChoice::windowLarge);
    CHECK (e.getBounds().getWidth() == 1760);
    CHECK (e.getBounds().getHeight() == 1000);

    e.applyViewChoice (ui::ViewChoice::windowMedium);
    CHECK (e.getBounds().getWidth() == 1440);
    CHECK (e.getBounds().getHeight() == 900);

    e.setUiScale (1.25);
    e.applyViewChoice (ui::ViewChoice::windowSmall);
    CHECK (e.getBounds().getWidth() == 1500);
    CHECK (e.getBounds().getHeight() == 950);
    CHECK (e.getPlan().width == 1200);
    CHECK (e.getPlan().height == 760);

    // A preset that does not fit the limits is held to them.
    e.setUiScale (2.0);
    e.applyViewChoice (ui::ViewChoice::windowLarge);
    CHECK (e.getBounds().getWidth() == ViewSettings::maxWindowWidth);
    CHECK (e.getBounds().getHeight() == 2000);
}

TEST_CASE ("A plugin opens at the smallest layout whatever size it was left at", "[view][editor]")
{
    CleanPreferences clean;
    showPanel();
    plugin::ScratchpadProcessor p;

    {
        ScratchpadEditor e (p);
        setSizeFor (e, 80);
    }

    ScratchpadEditor again (p); // only the standalone app opens at the size it was left
    CHECK (again.getWidth() == 1200);
    CHECK (again.getHeight() == 760);
    CHECK_FALSE (again.isExplainCollapsed()); // the panel's own choice is remembered
}

TEST_CASE ("A damaged preferences file still opens the editor at the defaults", "[view][editor]")
{
    CleanPreferences clean;
    const auto file = ViewSettings().getFile();
    file.getParentDirectory().createDirectory();
    REQUIRE (file.replaceWithText ("<PROPERTIES><VALUE name=\"uiScale\" val=\"banana\"/><VALUE name=\"theme\" val=\"neon"));

    plugin::ScratchpadProcessor p;
    ScratchpadEditor e (p);
    CHECK (e.getWidth() == 1200);
    CHECK_THAT (e.getUiScale(), WithinAbs (1.0, 1.0e-9));
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
        CHECK_FALSE (state.explainShown);

        e.applyViewChoice (ui::ViewChoice::themeLight);
        CHECK (e.getTheme() == ViewSettings::Theme::light);

        e.applyViewChoice (ui::ViewChoice::reducedMotion);
        CHECK (ui::motion::isReduced());
        CHECK (e.getViewState().reducedMotion);

        e.applyViewChoice (ui::ViewChoice::explainPanel); // folded -> shown
        CHECK_FALSE (e.isExplainCollapsed());
        CHECK (e.getViewState().explainShown);

        e.applyViewChoice (ui::ViewChoice::none);
        CHECK (e.getTheme() == ViewSettings::Theme::light);
    }

    // A later session: all three come back.
    ui::motion::setReduced (false);
    ScratchpadEditor again (p);
    CHECK (again.getTheme() == ViewSettings::Theme::light);
    CHECK (ui::motion::isReduced());
    CHECK_FALSE (again.isExplainCollapsed());
    CHECK (&ui::paletteOf (again) == &ui::lightPalette());

    again.applyViewChoice (ui::ViewChoice::themeDark);
    again.applyViewChoice (ui::ViewChoice::reducedMotion); // toggles back off
    again.applyViewChoice (ui::ViewChoice::explainPanel);  // shown -> folded
    CHECK (again.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (ui::motion::isReduced());
    CHECK (again.isExplainCollapsed());
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

    for (int percent : { 75, 100, 133 })
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

TEST_CASE ("A More pop-over opened in a hosted editor is drawn with the editor's look", "[view][popover]")
{
    CleanPreferences clean;
    // A dismissed call-out box is deleted on the message thread, which a test never runs, so its content (whose rows
    // listen to the processor's parameters) lives until JUCE shuts down. The processor must outlive it.
    static plugin::ScratchpadProcessor p;
    p.addModule (plugin::Module::filter);

    // The standalone app and plugin hosts put the editor inside a window of their own, with their own look and feel.
    juce::Component window, holder;
    window.setSize (1200, 760);
    window.addAndMakeVisible (holder);
    holder.setBounds (window.getLocalBounds());
    ScratchpadEditor e (p);
    holder.addAndMakeVisible (e);
    e.setBounds (holder.getLocalBounds());
    e.setTheme (ViewSettings::Theme::light);

    buttonTitled (e, "Filter more options").onClick();

    auto popovers = all<ui::MorePopover> (window);
    REQUIRE (popovers.size() == 1);
    CHECK (e.isParentOf (popovers.front()));
    CHECK (&ui::paletteOf (*popovers.front()) == &ui::lightPalette());

    popovers.front()->close();
}
