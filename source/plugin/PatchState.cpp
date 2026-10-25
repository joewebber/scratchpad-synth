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

    // Version 0 is an unstamped tree, version 1 is M1's, 2 is M2's, 3 is M3's and 4 is M4's.
    // Their parameter IDs are all still valid, and everything added since is
    // absent, so it loads at its defaults (LFOs not present, no connections,
    // nothing bypassed or soloed, no effects, effect order distortion, chorus, delay, reverb,
    // no unison, sync, ring or FM): only the stamp changes.
    // Later versions add their own `if (version < N)` steps here, in order.
    stamp (xml);
    return MigrationResult::migrated;
}

} // namespace scratchpad::patch
