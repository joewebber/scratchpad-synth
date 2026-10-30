#include "ui/ViewSettings.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

// Editors read and write the user's view preferences. A test run must never touch the real ones, nor depend on them,
// so every editor made during the run uses a throwaway settings file in a temporary folder.
class ViewSettingsIsolation : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting (const Catch::TestRunInfo&) override
    {
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("scratchpad-tests-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        scratchpad::ui::ViewSettings::setDefaultFileOverride (folder.getChildFile ("view.settings"));
    }

    void testRunEnded (const Catch::TestRunStats&) override
    {
        scratchpad::ui::ViewSettings::setDefaultFileOverride ({});
        folder.deleteRecursively();
    }

private:
    juce::File folder;
};

CATCH_REGISTER_LISTENER (ViewSettingsIsolation)
