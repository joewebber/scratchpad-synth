#pragma once

#include <algorithm>

namespace scratchpad::dsp
{

/** Click-free on/off gate: a linear ramp between 0 and 1.

    This is what sits between the oscillator and the output until the user adds
    an Amp Envelope. Ramping can be re-targeted mid-flight, so a note-off during
    the attack ramp (or a retrigger during the release) continues from the
    current level and never jumps. */
class Gate
{
public:
    static constexpr double declickSeconds = 0.002; // fixed 2 ms, see brief
    static constexpr double stealSeconds = 0.005;   // voice-steal fade, 5 ms

    void setSampleRate (double sr) noexcept { sampleRate = sr; }

    void open (double rampSeconds = declickSeconds) noexcept
    {
        targetOpen = true;
        ramp (1.0f, rampSeconds);
    }

    void close (double rampSeconds = declickSeconds) noexcept
    {
        targetOpen = false;
        ramp (-1.0f, rampSeconds);
    }

    void reset() noexcept
    {
        level = 0.0f;
        step = 0.0f;
        ramping = false;
        targetOpen = false;
    }

    float next() noexcept
    {
        if (ramping)
        {
            level += step;

            if (targetOpen ? level >= 1.0f : level <= 0.0f)
            {
                level = targetOpen ? 1.0f : 0.0f;
                ramping = false;
            }
        }

        return level;
    }

    float getLevel() const noexcept { return level; }
    bool isClosedAndSilent() const noexcept { return ! targetOpen && ! ramping; }
    bool isClosing() const noexcept { return ! targetOpen && ramping; }

private:
    /** `direction` is +1 to open, -1 to close. A full-scale ramp takes
        `rampSeconds`, so a partial ramp takes proportionally less. */
    void ramp (float direction, double rampSeconds) noexcept
    {
        const bool alreadyThere = direction > 0.0f ? level >= 1.0f : level <= 0.0f;

        if (alreadyThere)
        {
            ramping = false;
            return;
        }

        const double samples = std::max (1.0, rampSeconds * sampleRate);
        step = direction * (float) (1.0 / samples);
        ramping = true;
    }

    double sampleRate = 48000.0;
    float level = 0.0f;
    float step = 0.0f;
    bool ramping = false;
    bool targetOpen = false;
};

} // namespace scratchpad::dsp
