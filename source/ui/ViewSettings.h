#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

#include <memory>
#include <optional>

namespace scratchpad::ui
{

/** How the window looks to this user, kept apart from the patch: a patch file, a host session and Undo never
    carry any of it. Stored in a small properties file in the user's settings folder.

    Keys:  `theme` ("dark" or "light"), `reducedMotion` (true or false), `explainCollapsed` (true or false; absent
    means folded), `uiScale` (0.75 to 2.0: how big a knob, a line of text and a button are; the window's size never
    changes it) and `windowWidth` / `windowHeight` (the standalone app's window in screen pixels, remembered).

    A missing, unreadable or half-written file, and values that make no sense, all read as the defaults.
    Every write goes to disk straight away, after re-reading the file so that a second instance in the same
    process (or a second process) does not lose the other's changes. */
class ViewSettings
{
public:
    enum class Theme { dark, light };

    static constexpr double minScale = 0.75, maxScale = 2.0, defaultScale = 1.0;

    /** The window's limits in screen pixels: 900 x 570 is the smallest layout (1200 x 760) at 75%. */
    static constexpr int minWindowWidth = 900, minWindowHeight = 570, maxWindowWidth = 3200, maxWindowHeight = 2000;

    /** The user's settings file ("Scratchpad Synth.view" next to the standalone's own settings), unless
        `setDefaultFileOverride` has pointed somewhere else. */
    ViewSettings();

    /** A specific file, for tests. It need not exist; its folder is created when something is written. */
    explicit ViewSettings (const juce::File&);

    ~ViewSettings();

    /** Makes every later default-constructed ViewSettings use `file` (a default File restores the user's own).
        For tests, so they never read or write real settings. */
    static void setDefaultFileOverride (const juce::File& file);

    const juce::File& getFile() const { return settingsFile; }

    Theme getTheme() const;
    void setTheme (Theme);

    bool getReducedMotion() const;
    void setReducedMotion (bool);

    /** Empty until the user has chosen; then the choice wins whatever the scale. */
    std::optional<bool> getExplainCollapsedChoice() const;
    void setExplainCollapsed (bool);

    /** Whether the panel is folded: the user's choice, else folded (it starts closed). */
    bool isExplainCollapsed() const;

    /** The UI scale: 1.0 is the OS's own scale (JUCE's logical pixels already follow Windows' display scaling, so
        100% is right on a display set to 150%). Clamped to `minScale` .. `maxScale`; anything unreadable is 1.0. */
    double getUiScale() const;
    void setUiScale (double);

    static double clampScale (double scale) noexcept;

    /** The standalone window's last size, if one was saved and is sensible. */
    std::optional<juce::Point<int>> getWindowSize() const;
    void setWindowSize (int width, int height);

private:
    juce::String read (const char* key) const;
    void write (const char* key, const juce::String& value);

    juce::File settingsFile;
    juce::ApplicationProperties applicationProperties; // used when the file is the default one
    std::unique_ptr<juce::PropertiesFile> ownedFile;   // used when a file was given
    juce::PropertiesFile* file = nullptr;
};

} // namespace scratchpad::ui
