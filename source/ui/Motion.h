#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <memory>

namespace scratchpad::ui::motion
{

/** The one place the "Reduced motion" preference is decided. Everything that moves by itself (the scope, the
    level meter, the limiter's light, the envelope and LFO dots, the arcs round modulated knobs) asks here how
    often to update and whether to smooth. Pictures that only change when a parameter does are not live and
    ignore it.

    Reduced motion means: a live picture updates at most `reducedHz` times a second, and each update jumps straight
    to the new value, with no easing, falling decay or hold. */
inline constexpr int reducedHz = 2;
inline constexpr double reducedIntervalMs = 1000.0 / reducedHz;

inline std::atomic<bool>& reducedFlag() noexcept
{
    static std::atomic<bool> flag { false };
    return flag;
}

inline bool isReduced() noexcept { return reducedFlag().load (std::memory_order_relaxed); }

/** Message thread. Tells every LiveTimer to switch rate. */
void setReduced (bool);

/** The rate a live picture that normally updates `normalHz` times a second runs at. */
constexpr int liveHz (int normalHz, bool reduced) noexcept
{
    return reduced ? std::min (normalHz, reducedHz) : normalHz;
}

/** One step of smoothing: `shown` moves `amount` (0 to 1) of the way to `target`; reduced motion takes it all. */
constexpr float follow (float shown, float target, float amount, bool reduced) noexcept
{
    return reduced ? target : shown + (target - shown) * amount;
}

/** A falling readout (a meter or a spectrum bar): up at once, down by at most `fallPerStep` per step. Reduced
    motion shows the new value as it is, going down as well as up. */
constexpr float fall (float shown, float incoming, float fallPerStep, bool reduced) noexcept
{
    return reduced ? incoming : std::max (incoming, shown - fallPerStep);
}

/** For callbacks that arrive at the display's rate (the vertical blank): says when to do the update. Normal
    motion lets every call through; reduced motion lets one through per `reducedIntervalMs`.
    `nowMs` is any steadily increasing clock; passing it in keeps this testable without waiting. */
class Throttle
{
public:
    bool due (double nowMs, bool reduced) noexcept
    {
        if (reduced && started && nowMs - lastMs < reducedIntervalMs)
            return false;

        started = true;
        lastMs = nowMs;
        return true;
    }

private:
    double lastMs = 0.0;
    bool started = false;
};

/** A timer for a live picture: runs at its normal rate, or at `reducedHz` under reduced motion, and changes
    rate by itself when the preference changes while it runs. */
class LiveTimer
{
public:
    LiveTimer();
    virtual ~LiveTimer();

    LiveTimer (const LiveTimer&) = delete;
    LiveTimer& operator= (const LiveTimer&) = delete;

    struct Impl; // implementation detail, public so the preference's registry can name it

    /** The milliseconds between ticks while running, or 0 when stopped. */
    int getLiveIntervalMs() const noexcept;

protected:
    void startLive (int normalHz);
    void stopLive();

    /** Called at the live rate on the message thread. */
    virtual void liveTick() = 0;

private:
    std::unique_ptr<Impl> impl;
};

} // namespace scratchpad::ui::motion
