#include "plugin/PluginProcessor.h"
#include "ui/Meter.h"
#include "ui/ModulationModel.h"
#include "ui/Motion.h"
#include "ui/Scope.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace scratchpad::ui;
using Catch::Matchers::WithinAbs;

namespace
{
    /** Puts the preference back however the test left it. */
    struct ReducedMotionGuard
    {
        ReducedMotionGuard() { motion::setReduced (false); }
        ~ReducedMotionGuard() { motion::setReduced (false); }
    };

    class CountingTimer final : public motion::LiveTimer
    {
    public:
        using motion::LiveTimer::getLiveIntervalMs;
        using motion::LiveTimer::startLive;
        using motion::LiveTimer::stopLive;

    private:
        void liveTick() override {}
    };
}

TEST_CASE ("Reduced motion caps live updates at twice a second", "[motion]")
{
    CHECK (motion::liveHz (30, false) == 30);
    CHECK (motion::liveHz (60, false) == 60);
    CHECK (motion::liveHz (30, true) == 2);
    CHECK (motion::liveHz (60, true) == 2);

    // Something that already runs slower than that is not sped up.
    CHECK (motion::liveHz (1, true) == 1);
}

TEST_CASE ("Reduced motion takes each value at once, with no easing", "[motion]")
{
    CHECK_THAT (motion::follow (0.0f, 1.0f, 0.25f, false), WithinAbs (0.25, 1.0e-6));
    CHECK_THAT (motion::follow (0.0f, 1.0f, 0.25f, true), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (motion::follow (4.0f, 1.0f, 0.5f, true), WithinAbs (1.0, 1.0e-6));

    // A falling readout: up at once either way, down slowly unless motion is reduced.
    CHECK_THAT (motion::fall (-40.0f, -10.0f, 3.0f, false), WithinAbs (-10.0, 1.0e-6));
    CHECK_THAT (motion::fall (-10.0f, -40.0f, 3.0f, false), WithinAbs (-13.0, 1.0e-6));
    CHECK_THAT (motion::fall (-10.0f, -40.0f, 3.0f, true), WithinAbs (-40.0, 1.0e-6));
}

TEST_CASE ("The throttle lets frames through at the display rate, or twice a second when reduced", "[motion]")
{
    SECTION ("normal motion never holds anything back")
    {
        motion::Throttle t;

        for (int frame = 0; frame < 120; ++frame)
            CHECK (t.due (frame * (1000.0 / 60.0), false));
    }

    SECTION ("reduced motion lets one frame through per half second")
    {
        motion::Throttle t;
        int through = 0;

        for (int frame = 0; frame < 600; ++frame) // ten seconds of 60 Hz frames
            if (t.due (frame * (1000.0 / 60.0), true))
                ++through;

        CHECK (through >= 19);
        CHECK (through <= 21);
    }

    SECTION ("the edges of the interval")
    {
        motion::Throttle t;
        CHECK (t.due (1000.0, true));  // the first frame is not held back
        CHECK_FALSE (t.due (1000.0, true));
        CHECK_FALSE (t.due (1499.0, true));
        CHECK (t.due (1500.0, true));
        CHECK_FALSE (t.due (1999.9, true));
        CHECK (t.due (2000.0, true));
    }

    SECTION ("turning reduced motion off lets the very next frame through")
    {
        motion::Throttle t;
        CHECK (t.due (0.0, true));
        CHECK_FALSE (t.due (16.0, true));
        CHECK (t.due (32.0, false));
        CHECK (t.due (48.0, false));
    }
}

TEST_CASE ("The level meter eases down normally and jumps when motion is reduced", "[motion][meter]")
{
    // Full-scale peak: 0 dB, straight up.
    CHECK_THAT (nextMeterDb (-60.0f, 1.0f, false), WithinAbs (0.0, 1.0e-4));
    CHECK_THAT (nextMeterDb (-60.0f, 1.0f, true), WithinAbs (0.0, 1.0e-4));

    // Silence after a loud moment: about 20 dB a second at 60 frames a second, unless reduced.
    CHECK_THAT (nextMeterDb (0.0f, 0.0f, false), WithinAbs (-20.0 / 60.0, 1.0e-4));
    CHECK_THAT (nextMeterDb (0.0f, 0.0f, true), WithinAbs (-100.0, 1.0e-4));

    // A quieter peak than what is shown: the same.
    CHECK (nextMeterDb (-6.0f, 0.1f, false) > -6.4f);
    CHECK_THAT (nextMeterDb (-6.0f, 0.1f, true), WithinAbs (-20.0, 1.0e-3));
}

TEST_CASE ("The limiter light holds a touch for a quarter of a second, or just shows the latest reading when reduced", "[motion][meter]")
{
    SECTION ("normal motion")
    {
        LimiterHold hold;
        CHECK_FALSE (hold.step (0.0f, false));
        CHECK_FALSE (hold.step (0.05f, false)); // under the threshold
        CHECK (hold.step (2.0f, false));

        int framesLit = 0;
        while (hold.step (0.0f, false) && framesLit < 100)
            ++framesLit;

        CHECK (framesLit == 14); // 15 frames in all, counting the one that touched
    }

    SECTION ("reduced motion has no hold")
    {
        LimiterHold hold;
        CHECK (hold.step (2.0f, true));
        CHECK_FALSE (hold.step (0.0f, true));
        CHECK (hold.step (0.2f, true));
    }

    SECTION ("a hold begun in normal motion is dropped when motion is reduced")
    {
        LimiterHold hold;
        CHECK (hold.step (2.0f, false));
        CHECK_FALSE (hold.step (0.0f, true));
        CHECK_FALSE (hold.step (0.0f, false));
    }
}

TEST_CASE ("A live timer changes rate when the preference changes, while it runs", "[motion]")
{
    ReducedMotionGuard guard;
    CountingTimer timer;

    CHECK (timer.getLiveIntervalMs() == 0);

    timer.startLive (30);
    CHECK (timer.getLiveIntervalMs() == 1000 / 30);

    motion::setReduced (true);
    CHECK (timer.getLiveIntervalMs() == 500);

    motion::setReduced (false);
    CHECK (timer.getLiveIntervalMs() == 1000 / 30);

    // A timer started while motion is reduced starts at the reduced rate.
    timer.stopLive();
    CHECK (timer.getLiveIntervalMs() == 0);
    motion::setReduced (true);
    timer.startLive (30);
    CHECK (timer.getLiveIntervalMs() == 500);

    // A stopped timer is not started by the preference.
    timer.stopLive();
    motion::setReduced (false);
    CHECK (timer.getLiveIntervalMs() == 0);
}

TEST_CASE ("The scope and the modulation arcs follow the preference", "[motion][ui]")
{
    ReducedMotionGuard guard;
    scratchpad::plugin::ScratchpadProcessor p;

    Scope scope (p);
    ModulationModel model (p);
    CHECK (scope.getLiveIntervalMs() == 0);
    CHECK (model.getLiveIntervalMs() == 0);

    scope.setVisible (true);
    model.setActive (true);
    CHECK (scope.getLiveIntervalMs() == 1000 / 30);
    CHECK (model.getLiveIntervalMs() == 1000 / 30);

    motion::setReduced (true);
    CHECK (scope.getLiveIntervalMs() == 500);
    CHECK (model.getLiveIntervalMs() == 500);

    motion::setReduced (false);
    CHECK (scope.getLiveIntervalMs() == 1000 / 30);
    CHECK (model.getLiveIntervalMs() == 1000 / 30);

    // Hidden, they are quiet whatever the preference.
    scope.setVisible (false);
    model.setActive (false);
    motion::setReduced (true);
    CHECK (scope.getLiveIntervalMs() == 0);
    CHECK (model.getLiveIntervalMs() == 0);
}
