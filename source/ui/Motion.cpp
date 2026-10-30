#include "ui/Motion.h"

#include <vector>

namespace scratchpad::ui::motion
{

namespace
{
    /** Every LiveTimer, so a change of preference reaches them all. Shared, so it lives as long as any timer holds it. */
    struct Registry
    {
        std::vector<LiveTimer::Impl*> timers;
    };
}

struct LiveTimer::Impl final : private juce::Timer
{
    explicit Impl (LiveTimer& o) : owner (o) { registry->timers.push_back (this); }
    ~Impl() override { std::erase (registry->timers, this); }

    void start (int hz)
    {
        normalHz = hz;
        startTimerHz (liveHz (normalHz, isReduced()));
    }

    void stop() { stopTimer(); }
    int intervalMs() const noexcept { return isTimerRunning() ? getTimerInterval() : 0; }

    void rateChanged()
    {
        if (isTimerRunning())
            startTimerHz (liveHz (normalHz, isReduced()));
    }

private:
    void timerCallback() override { owner.liveTick(); }

    juce::SharedResourcePointer<Registry> registry;
    LiveTimer& owner;
    int normalHz = 30;
};

LiveTimer::LiveTimer() : impl (std::make_unique<Impl> (*this)) {}
LiveTimer::~LiveTimer() = default;

void LiveTimer::startLive (int normalHz) { impl->start (normalHz); }
void LiveTimer::stopLive() { impl->stop(); }
int LiveTimer::getLiveIntervalMs() const noexcept { return impl->intervalMs(); }

void setReduced (bool reduced)
{
    if (reducedFlag().exchange (reduced) == reduced)
        return;

    juce::SharedResourcePointer<Registry> registry;

    for (auto* t : std::vector<LiveTimer::Impl*> (registry->timers))
        t->rateChanged();
}

} // namespace scratchpad::ui::motion
