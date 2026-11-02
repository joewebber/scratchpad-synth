#include "ui/ViewSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using scratchpad::ui::ViewSettings;
using Catch::Matchers::WithinAbs;

namespace
{
    /** A settings file in a folder of its own, gone at the end of the test. */
    struct TempSettings
    {
        TempSettings()
            : folder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("scratchpad-viewsettings-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64())))
        {
        }

        ~TempSettings() { folder.deleteRecursively(); }

        juce::File file() const { return folder.getChildFile ("nested").getChildFile ("view.settings"); }

        void writeText (const juce::String& text)
        {
            file().getParentDirectory().createDirectory();
            REQUIRE (file().replaceWithText (text));
        }

        juce::File folder;
    };

    juce::String xmlWithValue (const char* key, const char* value)
    {
        return juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n  <VALUE name=\"") + key + "\" val=\"" + value
               + "\"/>\n</PROPERTIES>\n";
    }
}

TEST_CASE ("A missing settings file reads as the defaults", "[view][settings]")
{
    TempSettings temp;
    ViewSettings s (temp.file());

    CHECK (s.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (s.getReducedMotion());
    CHECK_FALSE (s.getExplainCollapsedChoice().has_value());
    CHECK_THAT (s.getUiScale(), WithinAbs (1.0, 1.0e-9));
    CHECK_FALSE (temp.file().exists()); // reading creates nothing
}

TEST_CASE ("Every setting survives being written and read back by a new object", "[view][settings]")
{
    TempSettings temp;

    {
        ViewSettings s (temp.file());
        s.setTheme (ViewSettings::Theme::light);
        s.setReducedMotion (true);
        s.setExplainCollapsed (false);
        s.setUiScale (0.9);
    }

    ViewSettings again (temp.file());
    CHECK (again.getTheme() == ViewSettings::Theme::light);
    CHECK (again.getReducedMotion());
    REQUIRE (again.getExplainCollapsedChoice().has_value());
    CHECK_FALSE (*again.getExplainCollapsedChoice());
    CHECK_THAT (again.getUiScale(), WithinAbs (0.9, 1.0e-6));
}

TEST_CASE ("The file uses the documented keys", "[view][settings]")
{
    TempSettings temp;

    {
        ViewSettings s (temp.file());
        s.setTheme (ViewSettings::Theme::light);
        s.setReducedMotion (true);
        s.setExplainCollapsed (true);
        s.setUiScale (0.8);
    }

    const auto xml = juce::XmlDocument::parse (temp.file());
    REQUIRE (xml != nullptr);

    juce::StringPairArray found;
    for (auto* v : xml->getChildWithTagNameIterator ("VALUE"))
        found.set (v->getStringAttribute ("name"), v->getStringAttribute ("val"));

    CHECK (found["theme"] == "light");
    CHECK (found["reducedMotion"] == "true");
    CHECK (found["explainCollapsed"] == "true");
    CHECK (found["uiScale"].getDoubleValue() == 0.8);
}

TEST_CASE ("Two objects on one file do not undo each other's changes", "[view][settings]")
{
    TempSettings temp;
    ViewSettings first (temp.file());
    ViewSettings second (temp.file());

    first.setTheme (ViewSettings::Theme::light);
    second.setReducedMotion (true); // `second` has not seen the theme change

    ViewSettings reader (temp.file());
    CHECK (reader.getTheme() == ViewSettings::Theme::light);
    CHECK (reader.getReducedMotion());
}

TEST_CASE ("A corrupt settings file reads as the defaults and can be written over", "[view][settings]")
{
    TempSettings temp;

    SECTION ("binary rubbish")
    {
        temp.file().getParentDirectory().createDirectory();
        const char rubbish[] = { 'x', '\0', '\x7f', '\x01', '<', '<', '<', '\xff', '\0' };
        REQUIRE (temp.file().replaceWithData (rubbish, sizeof (rubbish)));
    }

    SECTION ("an unfinished XML file")
    {
        temp.writeText ("<?xml version=\"1.0\"?>\n<PROPERTIES><VALUE name=\"theme\" val=\"li");
    }

    SECTION ("XML that is not a settings file")
    {
        temp.writeText ("<html><body>hello</body></html>");
    }

    SECTION ("an empty file")
    {
        temp.writeText ({});
    }

    ViewSettings s (temp.file());
    CHECK (s.getTheme() == ViewSettings::Theme::dark);
    CHECK_FALSE (s.getReducedMotion());
    CHECK_FALSE (s.getExplainCollapsedChoice().has_value());
    CHECK_THAT (s.getUiScale(), WithinAbs (1.0, 1.0e-9));

    s.setTheme (ViewSettings::Theme::light);
    s.setUiScale (0.9);

    ViewSettings again (temp.file());
    CHECK (again.getTheme() == ViewSettings::Theme::light);
    CHECK_THAT (again.getUiScale(), WithinAbs (0.9, 1.0e-6));
}

TEST_CASE ("Values that make no sense read as the defaults or are pulled into range", "[view][settings]")
{
    TempSettings temp;

    const auto scaleFor = [&temp] (const char* text)
    {
        temp.writeText (xmlWithValue ("uiScale", text));
        return ViewSettings (temp.file()).getUiScale();
    };

    CHECK_THAT (scaleFor ("0.9"), WithinAbs (0.9, 1.0e-9));
    CHECK_THAT (scaleFor ("0.75"), WithinAbs (0.75, 1.0e-9));
    CHECK_THAT (scaleFor ("1.5"), WithinAbs (1.5, 1.0e-9));
    CHECK_THAT (scaleFor ("2"), WithinAbs (2.0, 1.0e-9));

    // Out of range: the nearest scale the window allows.
    CHECK_THAT (scaleFor ("0.1"), WithinAbs (0.75, 1.0e-9));
    CHECK_THAT (scaleFor ("-3"), WithinAbs (0.75, 1.0e-9));
    CHECK_THAT (scaleFor ("9"), WithinAbs (2.0, 1.0e-9));
    CHECK_THAT (scaleFor ("1e9"), WithinAbs (2.0, 1.0e-9));

    // Not a number at all: the default, not "0 clamped up to the minimum".
    CHECK_THAT (scaleFor ("big"), WithinAbs (1.0, 1.0e-9));
    CHECK_THAT (scaleFor (""), WithinAbs (1.0, 1.0e-9));
    CHECK_THAT (scaleFor ("nan"), WithinAbs (1.0, 1.0e-9));

    temp.writeText (xmlWithValue ("theme", "purple"));
    CHECK (ViewSettings (temp.file()).getTheme() == ViewSettings::Theme::dark);

    temp.writeText (xmlWithValue ("explainCollapsed", "maybe"));
    CHECK_FALSE (ViewSettings (temp.file()).getExplainCollapsedChoice().has_value());

    temp.writeText (xmlWithValue ("reducedMotion", "yes"));
    CHECK_FALSE (ViewSettings (temp.file()).getReducedMotion());
}

TEST_CASE ("Scales written are clamped to the window's limits", "[view][settings]")
{
    TempSettings temp;
    ViewSettings s (temp.file());

    s.setUiScale (50.0);
    CHECK_THAT (s.getUiScale(), WithinAbs (2.0, 1.0e-9));

    s.setUiScale (0.0);
    CHECK_THAT (s.getUiScale(), WithinAbs (0.75, 1.0e-9));

    CHECK_THAT (ViewSettings::clampScale (std::numeric_limits<double>::infinity()), WithinAbs (1.0, 1.0e-9));
}

TEST_CASE ("The standalone's window size is kept, and nonsense reads as none", "[view][settings]")
{
    TempSettings temp;
    ViewSettings s (temp.file());
    CHECK_FALSE (s.getWindowSize().has_value());

    s.setWindowSize (1440, 900);
    REQUIRE (ViewSettings (temp.file()).getWindowSize().has_value());
    CHECK (*ViewSettings (temp.file()).getWindowSize() == juce::Point<int> (1440, 900));

    // Written sizes are held to the window's limits.
    s.setWindowSize (50000, 10);
    CHECK (*s.getWindowSize() == juce::Point<int> (ViewSettings::maxWindowWidth, ViewSettings::minWindowHeight));

    // A hand-edited file with a size outside them, or not a number, reads as none.
    temp.writeText ("<PROPERTIES><VALUE name=\"windowWidth\" val=\"100\"/><VALUE name=\"windowHeight\" val=\"900\"/></PROPERTIES>");
    CHECK_FALSE (ViewSettings (temp.file()).getWindowSize().has_value());
    temp.writeText ("<PROPERTIES><VALUE name=\"windowWidth\" val=\"wide\"/><VALUE name=\"windowHeight\" val=\"900\"/></PROPERTIES>");
    CHECK_FALSE (ViewSettings (temp.file()).getWindowSize().has_value());
}

TEST_CASE ("The Explain panel starts folded until the user chooses", "[view][settings]")
{
    TempSettings temp;
    ViewSettings s (temp.file());

    CHECK (s.isExplainCollapsed());

    s.setExplainCollapsed (false);
    CHECK_FALSE (s.isExplainCollapsed());

    s.setExplainCollapsed (true);
    CHECK (s.isExplainCollapsed());
}

TEST_CASE ("The default settings file can be redirected, as the tests do", "[view][settings]")
{
    TempSettings temp;
    const auto before = ViewSettings().getFile();

    ViewSettings::setDefaultFileOverride (temp.file());
    CHECK (ViewSettings().getFile() == temp.file());

    ViewSettings::setDefaultFileOverride (before);
    CHECK (ViewSettings().getFile() == before);
}
