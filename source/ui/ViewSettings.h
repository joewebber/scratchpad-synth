#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>
#include <optional>

namespace scratchpad::ui
{

/** How the window looks to this user, kept apart from the patch: a patch file, a host session and Undo never
    carry any of it. Stored in a small properties file in the user's settings folder.

    Keys:  `theme` ("dark" or "light"), `reducedMotion` (true or false), `explainCollapsed` (true or false; absent
    means folded) and `editorScale` (0.75 to 1.0: how far the stage is shrunk to fit a window smaller than the design; it is never scaled up).

    A missing, unreadable or half-written file, and values that make no sense, all read as the defaults.
    Every write goes to disk straight away, after re-reading the file so that a second instance in the same
    process (or a second process) does not lose the other's changes. */
class ViewSettings
{
public:
    enum class Theme { dark, light };

    static constexpr double minScale = 0.75, maxScale = 1.0, defaultScale = 1.0;


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

    double getEditorScale() const;
    void setEditorScale (double);

    static double clampScale (double scale) noexcept;

private:
    juce::String read (const char* key) const;
    void write (const char* key, const juce::String& value);

    juce::File settingsFile;
    juce::ApplicationProperties applicationProperties; // used when the file is the default one
    std::unique_ptr<juce::PropertiesFile> ownedFile;   // used when a file was given
    juce::PropertiesFile* file = nullptr;
};

} // namespace scratchpad::ui
