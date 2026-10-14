#pragma once

#include <juce_core/juce_core.h>

namespace scratchpad::patch
{

/** Bump when the saved XML changes shape, and add a step to `migrate`. */
inline constexpr int currentSchemaVersion = 2;

inline constexpr const char* schemaVersionAttribute = "schemaVersion";

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
