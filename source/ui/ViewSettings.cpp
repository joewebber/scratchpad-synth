#include "ui/ViewSettings.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::ui
{

namespace
{
    constexpr const char* themeKey = "theme";
    constexpr const char* reducedMotionKey = "reducedMotion";
    constexpr const char* explainCollapsedKey = "explainCollapsed";
    constexpr const char* uiScaleKey = "uiScale";
    constexpr const char* windowWidthKey = "windowWidth";
    constexpr const char* windowHeightKey = "windowHeight";

    /** A whole number of pixels written by this class, or nothing for anything else. */
    std::optional<int> readInt (const juce::String& text)
    {
        const auto t = text.trim();

        if (t.isEmpty() || ! t.containsOnly ("0123456789"))
            return std::nullopt;

        return t.getIntValue();
    }

    juce::File& defaultFileOverride()
    {
        static juce::File file;
        return file;
    }

    juce::PropertiesFile::Options optionsFor (juce::StringRef suffix)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Scratchpad Synth";
        options.filenameSuffix = suffix;
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = 0; // every write goes straight to disk
        return options;
    }
}

ViewSettings::ViewSettings()
{
    if (const auto& forced = defaultFileOverride(); forced != juce::File())
    {
        settingsFile = forced;
        ownedFile = std::make_unique<juce::PropertiesFile> (forced, optionsFor (".view"));
        file = ownedFile.get();
        return;
    }

    applicationProperties.setStorageParameters (optionsFor (".view"));
    file = applicationProperties.getUserSettings();

    if (file != nullptr)
        settingsFile = file->getFile();
}

ViewSettings::ViewSettings (const juce::File& f)
    : settingsFile (f),
      ownedFile (std::make_unique<juce::PropertiesFile> (f, optionsFor (".view"))),
      file (ownedFile.get())
{
}

ViewSettings::~ViewSettings()
{
    if (file != nullptr)
        file->saveIfNeeded();
}

void ViewSettings::setDefaultFileOverride (const juce::File& f)
{
    defaultFileOverride() = f;
}

juce::String ViewSettings::read (const char* key) const
{
    return file != nullptr ? file->getValue (key) : juce::String();
}

void ViewSettings::write (const char* key, const juce::String& value)
{
    if (file == nullptr)
        return;

    // Pick up what another instance wrote since this one looked, then change just this key.
    file->getFile().getParentDirectory().createDirectory();
    file->reload();
    file->setValue (key, value);
    file->saveIfNeeded();
}

ViewSettings::Theme ViewSettings::getTheme() const
{
    return read (themeKey) == "light" ? Theme::light : Theme::dark;
}

void ViewSettings::setTheme (Theme t)
{
    write (themeKey, t == Theme::light ? "light" : "dark");
}

bool ViewSettings::getReducedMotion() const
{
    return read (reducedMotionKey) == "true";
}

void ViewSettings::setReducedMotion (bool on)
{
    write (reducedMotionKey, on ? "true" : "false");
}

std::optional<bool> ViewSettings::getExplainCollapsedChoice() const
{
    const auto text = read (explainCollapsedKey);

    if (text == "true")
        return true;

    if (text == "false")
        return false;

    return std::nullopt;
}

void ViewSettings::setExplainCollapsed (bool collapsed)
{
    write (explainCollapsedKey, collapsed ? "true" : "false");
}

bool ViewSettings::isExplainCollapsed() const
{
    return getExplainCollapsedChoice().value_or (true);
}

double ViewSettings::clampScale (double scale) noexcept
{
    return std::isfinite (scale) ? std::clamp (scale, minScale, maxScale) : defaultScale;
}

double ViewSettings::getUiScale() const
{
    const auto text = read (uiScaleKey).trim();

    // getDoubleValue reads rubbish as 0, which would pass for a real (too small) scale.
    if (! text.containsOnly ("0123456789.+-eE") || ! text.containsAnyOf ("0123456789"))
        return defaultScale;

    return clampScale (text.getDoubleValue());
}

void ViewSettings::setUiScale (double scale)
{
    write (uiScaleKey, juce::String (clampScale (scale), 4));
}

std::optional<juce::Point<int>> ViewSettings::getWindowSize() const
{
    const auto w = readInt (read (windowWidthKey)), h = readInt (read (windowHeightKey));

    if (! w || ! h || *w < minWindowWidth || *w > maxWindowWidth || *h < minWindowHeight || *h > maxWindowHeight)
        return std::nullopt;

    return juce::Point<int> { *w, *h };
}

void ViewSettings::setWindowSize (int width, int height)
{
    write (windowWidthKey, juce::String (std::clamp (width, minWindowWidth, maxWindowWidth)));
    write (windowHeightKey, juce::String (std::clamp (height, minWindowHeight, maxWindowHeight)));
}

} // namespace scratchpad::ui
