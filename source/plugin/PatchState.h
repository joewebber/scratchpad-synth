#pragma once

#include <juce_core/juce_core.h>

namespace scratchpad::patch
{

/** Bump when the saved XML changes shape, and add a step to `migrate`. */
inline constexpr int currentSchemaVersion = 6;

inline constexpr const char* schemaVersionAttribute = "schemaVersion";

/** The state tree's non-parameter property holding the patch's name (the file stem it was last saved or opened as).
    Absent means the patch is untitled. */
inline constexpr const char* nameProperty = "patchName";
inline constexpr const char* untitledName = "Untitled patch";

/** User patch files: the same XML the host saves, in `<Documents>/Scratchpad Synth/Patches`. */
inline constexpr const char* fileExtension = ".scratchpad";
inline constexpr const char* fileWildcard = "*.scratchpad";

/** `file` with the patch extension: unchanged if it already has it, otherwise the extension is appended. */
juce::File withFileExtension (const juce::File& file);

/** Stamps the current schema version onto an XML state tree. */
void stamp (juce::XmlElement& xml);

enum class MigrationResult
{
    upToDate,
    migrated,
    tooNew, // saved by a newer version of the synth; refuse to load
    invalid // not a Scratchpad state
};

/** Upgrades `xml` in place to `currentSchemaVersion`. `rootTag` is the
    expected tag of the state tree. Files with no version are version 0. */
MigrationResult migrate (juce::XmlElement& xml, const juce::String& rootTag);

} // namespace scratchpad::patch
