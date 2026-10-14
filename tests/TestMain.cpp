#include <catch2/catch_session.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <juce_events/juce_events.h>

#include <memory>

// Parameter trees and undo managers expect a message manager to exist.
class JuceInitialiser : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting (const Catch::TestRunInfo&) override
    {
        initialiser = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    }

    void testRunEnded (const Catch::TestRunStats&) override { initialiser.reset(); }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> initialiser;
};

CATCH_REGISTER_LISTENER (JuceInitialiser)
