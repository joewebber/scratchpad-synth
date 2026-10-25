#include "plugin/Suggestion.h"

#include <catch2/catch_test_macros.hpp>

using namespace scratchpad::plugin;

namespace
{
    Presence with (std::initializer_list<Module> modules)
    {
        Presence p {};
        for (const auto m : modules)
            p[(size_t) m] = true;
        return p;
    }
}

TEST_CASE ("The suggestion follows the path and skips what is already there", "[suggestion]")
{
    REQUIRE (suggestNext (with ({}))->module == Module::ampEnv);
    REQUIRE (suggestNext (with ({ Module::ampEnv }))->module == Module::osc2);
    REQUIRE (suggestNext (with ({ Module::ampEnv, Module::osc2 }))->module == Module::filter);
    REQUIRE (suggestNext (with ({ Module::ampEnv, Module::osc2, Module::filter }))->module == Module::filterEnv);

    // Someone who jumped ahead is advised about what is still missing, in path order.
    REQUIRE (suggestNext (with ({ Module::filter, Module::osc2 }))->module == Module::ampEnv);
    REQUIRE (suggestNext (with ({ Module::ampEnv, Module::filter, Module::filterEnv, Module::lfo1 }))->module == Module::osc2);
}

TEST_CASE ("With every other module in, LFO 2 is next and then there is nothing to say", "[suggestion]")
{
    Presence all {};
    all.fill (true);
    all[(size_t) Module::lfo2] = false;

    REQUIRE (suggestNext (all)->module == Module::lfo2);

    all[(size_t) Module::lfo2] = true;
    REQUIRE_FALSE (suggestNext (all).has_value());
}

TEST_CASE ("Every module has a reason", "[suggestion]")
{
    for (const auto m : allModules)
        REQUIRE (*reasonFor (m) != '\0');
}

TEST_CASE ("After LFO 2 the path walks the effects: delay, reverb, chorus, distortion", "[suggestion]")
{
    Presence p {};
    p.fill (true);

    for (const auto m : { Module::delay, Module::reverb, Module::chorus, Module::distortion })
        p[(size_t) m] = false;

    for (const auto expected : { Module::delay, Module::reverb, Module::chorus, Module::distortion })
    {
        const auto next = suggestNext (p);
        REQUIRE (next.has_value());
        REQUIRE (next->module == expected);
        REQUIRE (*next->reason != '\0');
        p[(size_t) expected] = true;
    }

    REQUIRE_FALSE (suggestNext (p).has_value());

    // Effects wait for the core path: a missing LFO 2 is advised first, and an effect that was jumped to is skipped.
    Presence early {};
    early.fill (true);
    early[(size_t) Module::lfo2] = false;
    early[(size_t) Module::delay] = false;
    REQUIRE (suggestNext (early)->module == Module::lfo2);

    early[(size_t) Module::lfo2] = true;
    early[(size_t) Module::delay] = true;
    early[(size_t) Module::reverb] = false;
    REQUIRE (suggestNext (early)->module == Module::reverb);
}
