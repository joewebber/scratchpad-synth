#pragma once

#include "dsp/FastTanh.h"
#include "dsp/FractionalRead.h"
#include "dsp/LadderFilter.h"
#include "dsp/SvfFilter.h"

#include "dsp/FloatCompare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace scratchpad::dsp
{

/** Filter modes. The integer values are stored in saved patches
    (`filter.mode`), so never reorder them. */
enum class FilterMode { lp24 = 0, lp12 = 1, hp12 = 2, bp12 = 3, notch = 4, comb = 5 };

inline constexpr int numFilterModes = 6;

/** The brief's multimode filter: a `tanh` drive stage, then a 4-pole ladder
    for LP 24 or the state-variable filter for LP 12, HP 12 and BP 12.

    Changing mode crossfades over 10 ms, running the outgoing and incoming
    filters side by side, so the switch does not click. Cutoff is clamped to
    20 Hz .. 0.45 x the sample rate. */
class MultimodeFilter
{
public:
    static constexpr double switchSeconds = 0.010;

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        ladder.setSampleRate (sr);
        svf.setSampleRate (sr);
        fadeLength = std::max (1, (int) (switchSeconds * sr));
        combDelay.assign ((size_t) (sr / minCombHz) + 8, 0.0f); // long enough for a 20 Hz comb at any sample rate
        reset();
    }

    void reset() noexcept
    {
        ladder.reset();
        svf.reset();
        std::fill (combDelay.begin(), combDelay.end(), 0.0f);
        combIndex = 0;
        combDcIn = combDcOut = 0.0f;
        combCurrent = combTarget;
        fadeRemaining = 0;
        previousMode = mode;
    }

    /** Takes effect with a crossfade, unless it is already the mode. */
    void setMode (FilterMode newMode) noexcept
    {
        if (newMode == mode)
            return;

        previousMode = mode;
        mode = newMode;
        fadeRemaining = fadeLength;
    }

    /** Switches without a crossfade, for a voice that is just starting. */
    void setModeImmediately (FilterMode newMode) noexcept
    {
        mode = newMode;
        previousMode = newMode;
        fadeRemaining = 0;
    }

    FilterMode getMode() const noexcept { return mode; }

    /** `resonance` is 0 to 1, `driveDb` 0 to 24. Cheap enough to call every sample. */
    void setParams (float cutoffHz, float resonance, float driveDb) noexcept
    {
        const float hz = std::clamp (cutoffHz, 20.0f, (float) (0.45 * sampleRate));
        ladder.setParams (hz, resonance, driveDb);
        svf.setParams (hz, resonance);
        if (! combDelay.empty())
            combTarget = (float) std::clamp ((double) sampleRate / (double) std::max (minCombHz, hz), 2.0, (double) combDelay.size() - 4.0);
        combFeedback = 0.15f + 0.80f * std::clamp (resonance, 0.0f, 1.0f);
        if (! same (driveDb, lastDriveDb))
        {
            lastDriveDb = driveDb;
            drive = std::pow (10.0f, std::max (driveDb, 0.0f) / 20.0f);
            driveMix = std::clamp (driveDb / 6.0f, 0.0f, 1.0f);
            driveMakeup = std::pow (drive, -0.05f);
        }
    }

    float process (float x) noexcept
    {
        if (driveMix > 0.0f)
        {
            const float driven = fastTanh (drive * x) * driveMakeup;
            x += (driven - x) * driveMix;
        }

        const bool needLadder = mode == FilterMode::lp24 || (fadeRemaining > 0 && previousMode == FilterMode::lp24);
        const bool needSvf = (mode != FilterMode::lp24 && mode != FilterMode::comb)
                          || (fadeRemaining > 0 && previousMode != FilterMode::lp24 && previousMode != FilterMode::comb);
        const float ladderOut = needLadder ? ladder.process (x) : 0.0f;
        const auto svfOut = needSvf ? svf.process (x) : SvfFilter::Outputs { 0.0f, 0.0f, 0.0f, 0.0f };
        const float combOut = (mode == FilterMode::comb || (fadeRemaining > 0 && previousMode == FilterMode::comb)) ? processComb (x) : 0.0f;

        const float now = pick (mode, ladderOut, svfOut, combOut);

        if (fadeRemaining <= 0)
            return now;

        const float before = pick (previousMode, ladderOut, svfOut, combOut);
        const float t = 1.0f - (float) fadeRemaining / (float) fadeLength;
        --fadeRemaining;
        return before + (now - before) * t;
    }

private:
    /** A feedback comb tuned to the cutoff: a fractional delay (so it tracks pitch to a fraction of a cent and
        sweeps without steps), the input DC-blocked (a feedback of 0.95 would otherwise ring 20x on DC), and the
        output scaled by `1 - feedback` so the peaks keep unity gain however much resonance there is. */
    float processComb (float x) noexcept
    {
        if (combDelay.empty())
            return 0.0f;

        const float blockedIn = x - combDcIn + 0.9985f * combDcOut;
        combDcIn = x;
        combDcOut = blockedIn;

        combCurrent += (combTarget - combCurrent) * 0.02f;

        const int size = (int) combDelay.size();
        const float delayed = readCubic (combDelay.data(), size, (int) combIndex, combCurrent);
        combDelay[combIndex] = blockedIn + delayed * combFeedback;
        combIndex = (combIndex + 1) % combDelay.size();
        return delayed * (1.0f - combFeedback);
    }

    static float pick (FilterMode m, float ladderOut, const SvfFilter::Outputs& s, float combOut) noexcept
    {
        switch (m)
        {
            case FilterMode::lp24: return ladderOut;
            case FilterMode::lp12: return s.low;
            case FilterMode::hp12: return s.high;
            case FilterMode::bp12: return s.band;
            case FilterMode::notch: return s.notch;
            case FilterMode::comb: return combOut;
        }
        return 0.0f;
    }

    double sampleRate = 48000.0;
    LadderFilter ladder;
    SvfFilter svf;
    static constexpr float minCombHz = 20.0f;
    std::vector<float> combDelay; // sized in prepare
    size_t combIndex = 0;
    float combTarget = 100.0f, combCurrent = 100.0f;
    float combFeedback = 0.15f;
    float combDcIn = 0.0f, combDcOut = 0.0f;
    FilterMode mode = FilterMode::lp24, previousMode = FilterMode::lp24;
    int fadeLength = 480, fadeRemaining = 0;
    float lastDriveDb = -1.0f, drive = 1.0f, driveMix = 0.0f, driveMakeup = 1.0f;
};

} // namespace scratchpad::dsp
