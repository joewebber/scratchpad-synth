#include "plugin/PatchState.h"

namespace scratchpad::patch
{

void stamp (juce::XmlElement& xml)
{
    xml.setAttribute (schemaVersionAttribute, currentSchemaVersion);
}

MigrationResult migrate (juce::XmlElement& xml, const juce::String& rootTag)
{
    if (! xml.hasTagName (rootTag))
        return MigrationResult::invalid;

    const int version = xml.getIntAttribute (schemaVersionAttribute, 0);

    if (version > currentSchemaVersion)
        return MigrationResult::tooNew;

    if (version == currentSchemaVersion)
        return MigrationResult::upToDate;

    // Version 0 is an unstamped tree. Its parameter IDs are the same as
    // version 1, so the only change is the stamp. Later versions add their
    // own `if (version < N)` steps here, in order.
    stamp (xml);
    return MigrationResult::migrated;
}

} // namespace scratchpad::patch
