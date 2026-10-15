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

TEST_CASE ("The path ends with LFO 2 and then has nothing to say", "[suggestion]")
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
