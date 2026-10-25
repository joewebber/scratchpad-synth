#include "engine/OscSection.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::engine
{

namespace
{
    /** Moves `g` toward `t` with a one-pole step, landing on `t` once close enough. */
    inline float settle (float g, float t, float coef) noexcept
    {
        g += (t - g) * coef;
        return std::abs (t - g) < 1.0e-5f ? t : g;
    }

    /** Equal-power pan of unison copy `u` (-1 .. +1) of `n`, scaled by 1 / sqrt (n). */
    void panGains (double u, int n, float spread, float& l, float& r) noexcept
    {
        if (n == 1)
        {
            l = r = 1.0f;
            return;
        }

        const float norm = (float) (1.0 / std::sqrt ((double) n));

        if (spread <= 0.0f)
        {
            l = r = norm;
            return;
        }

        // Position -1 .. +1 maps to 0 .. pi/2; sqrt 2 puts the centre at unity per side.
        const double angle = (u * (double) spread + 1.0) * (std::numbers::pi / 4.0);
        l = (float) (std::numbers::sqrt2 * std::cos (angle)) * norm;
        r = (float) (std::numbers::sqrt2 * std::sin (angle)) * norm;
    }
}

void OscSection::prepare (double sampleRate, int os, std::uint32_t seed) noexcept
{
    oversampling = std::clamp (os, 1, 2);
    rateScale = 1.0 / (double) oversampling;

    const double tickRate = sampleRate * (double) oversampling;
    fadeTicks = std::max (1, (int) (switchSeconds * tickRate));
    bandLimitStep = (float) (1.0 / std::max (1.0, switchSeconds * tickRate));
    gainCoef = (float) (1.0 - std::exp (-1.0 / std::max (1.0, gainSmoothingSeconds * tickRate)));
    rng = seed * 2654435761u + 0x9e3779b9u;
    if (rng == 0u)
        rng = 1u;

    clear();
}

void OscSection::clear() noexcept
{
    stop();

    for (auto& b : banks)
    {
        b.count = 1;

        for (size_t c = 0; c < b.copies.size(); ++c)
        {
            auto& cp = b.copies[c];
            cp.osc.reset (c == 0 ? 0.0 : random());
            cp.ratio = 1.0;
            cp.ratioStep = 0.0;
            cp.gainL = cp.gainR = cp.targetL = cp.targetR = 0.0f;
        }
    }
}

void OscSection::stop() noexcept
{
    for (size_t i = 0; i < banks.size(); ++i)
        stopBank (i);
}

void OscSection::stopBank (size_t index) noexcept
{
    auto& b = banks[index];
    b.running = false;
    b.sync = false;
    b.fadeRemaining = 0;

    for (auto& cp : b.copies)
        cp.active = false;

    for (auto& d : decimators[index])
        d.reset();
}

int OscSection::masterCopyFor (int copy, int copies, int masterCopies) noexcept
{
    if (masterCopies <= 1)
        return 0;

    if (copies <= 1)
        return (masterCopies - 1) / 2; // the middle copy

    return (int) std::lround ((double) copy * (double) (masterCopies - 1) / (double) (copies - 1));
}

void OscSection::beginBlock (const std::array<BankSettings, 3>& settings, int numSamples) noexcept
{
    for (size_t k = 0; k < banks.size(); ++k)
        beginBank (k, settings[k], numSamples);

    // Oscillators 2 and 3 listen to oscillator 1, which must be running for there to be anything to hear.
    for (size_t k = 1; k < banks.size(); ++k)
    {
        auto& b = banks[k];
        b.sync = b.running && settings[k].sync && banks[0].running;

        if (b.sync)
            for (int c = 0; c < b.count; ++c)
                b.copies[(size_t) c].masterCopy = masterCopyFor (c, b.count, banks[0].count);
    }
}

void OscSection::beginBank (size_t index, const BankSettings& s, int numSamples) noexcept
{
    if (! s.on)
    {
        stopBank (index);
        return;
    }

    auto& b = banks[index];
    const int n = std::clamp (s.unison, 1, maxUnison);
    const double ticks = (double) (numSamples * oversampling);
    const bool starting = ! b.running;

    if (starting)
    {
        b.wave = b.waveFrom = s.wave;
        b.fadeRemaining = 0;
        b.increment = s.increment;
        b.pulseWidth = s.pulseWidth;
        b.bandLimit = s.antiAlias ? 1.0f : 0.0f;
        b.running = true;

        for (auto& cp : b.copies)
            cp.active = false;
    }
    else if (s.wave != b.wave)
    {
        b.waveFrom = b.wave;
        b.wave = s.wave;
        b.fadeRemaining = fadeTicks;
    }

    b.count = n;
    b.bandLimitTarget = s.antiAlias ? 1.0f : 0.0f;
    b.incrementStep = (s.increment - b.increment) / ticks;
    b.widthStep = (s.pulseWidth - b.pulseWidth) * (1.0f / (float) ticks);

    for (int c = 0; c < maxUnison; ++c)
    {
        auto& cp = b.copies[(size_t) c];

        if (c >= n)
        {
            cp.targetL = cp.targetR = 0.0f; // fades out, then stops
            cp.ratioStep = 0.0;
            continue;
        }

        const double u = n == 1 ? 0.0 : 2.0 * (double) c / (double) (n - 1) - 1.0;
        const double ratio = n == 1 || s.detuneCents == 0.0f ? 1.0 : std::exp2 (u * (double) s.detuneCents / 1200.0);

        float gl, gr;
        panGains (u, n, s.spread, gl, gr);

        if (! cp.active)
        {
            if (starting)
                cp.osc.reset (s.phaseReset ? (c == 0 ? 0.0 : random()) : cp.osc.getPhase());
            else
                cp.osc.jumpTo (random());

            cp.active = true;
            cp.ratio = ratio;

            // A copy that joins a sounding note fades in from silence.
            cp.gainL = starting ? gl : 0.0f;
            cp.gainR = starting ? gr : 0.0f;
        }

        cp.targetL = gl;
        cp.targetR = gr;
        cp.ratioStep = (ratio - cp.ratio) / ticks;
    }
}

double OscSection::random() noexcept
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (double) (rng >> 8) * (1.0 / 16777216.0);
}

float OscSection::modulator() const noexcept
{
    const auto& b = banks[0];
    float m = 0.0f;

    for (const auto& cp : b.copies)
        if (cp.active)
            m += (float) dsp::naiveWave (b.wave, cp.osc.getPhase(), (double) b.pulseWidth) * (0.5f * (cp.gainL + cp.gainR));

    return m;
}

StereoSample OscSection::tickBank (Bank& b, const Bank* master, double phaseOffset) noexcept
{
    StereoSample out;

    if (! b.running)
        return out;

    if (b.bandLimit < b.bandLimitTarget)
        b.bandLimit = std::min (b.bandLimitTarget, b.bandLimit + bandLimitStep);
    else if (b.bandLimit > b.bandLimitTarget)
        b.bandLimit = std::max (b.bandLimitTarget, b.bandLimit - bandLimitStep);

    b.increment += b.incrementStep;
    b.pulseWidth += b.widthStep;

    const bool fading = b.fadeRemaining > 0;
    float fadeMix = 0.0f;
    if (fading)
    {
        fadeMix = 1.0f - (float) b.fadeRemaining / (float) fadeTicks;
        --b.fadeRemaining;
    }

    for (size_t c = 0; c < b.copies.size(); ++c)
    {
        auto& cp = b.copies[c];

        if (! cp.active)
            continue;

        cp.ratio += cp.ratioStep;
        cp.osc.setIncrement (b.increment * cp.ratio * rateScale);
        cp.osc.setPulseWidth ((double) b.pulseWidth);
        cp.osc.setBandLimit (b.bandLimit);
        cp.osc.setPhaseOffset (phaseOffset);

        if (master != nullptr)
        {
            const auto& m = master->copies[(size_t) cp.masterCopy];

            if (m.active)
            {
                const double f = m.osc.wrapFraction();
                if (f >= 0.0)
                    cp.osc.syncTo (f);
            }
        }

        const float s = fading ? cp.osc.process (b.waveFrom, b.wave, fadeMix) : cp.osc.process (b.wave);

        out.l += s * cp.gainL;
        out.r += s * cp.gainR;

        if (cp.gainL != cp.targetL)
            cp.gainL = settle (cp.gainL, cp.targetL, gainCoef);

        if (cp.gainR != cp.targetR)
            cp.gainR = settle (cp.gainR, cp.targetR, gainCoef);

        if (cp.gainL == 0.0f && cp.gainR == 0.0f && cp.targetL == 0.0f && cp.targetR == 0.0f)
            cp.active = false;
    }

    return out;
}

void OscSection::step (float fmTo2, float fmTo3, StereoOscs& out) noexcept
{
    // Every oscillator reads oscillator 1 (its modulator and its phase, for sync) as it stands
    // before this sample, so oscillator 1 is advanced last.
    const double cyclesPerUnit = (fmTo2 != 0.0f || fmTo3 != 0.0f) ? (double) modulator() * fmCyclesAtFull : 0.0;

    out[1] = tickBank (banks[1], banks[1].sync ? &banks[0] : nullptr, cyclesPerUnit * (double) fmTo2);
    out[2] = tickBank (banks[2], banks[2].sync ? &banks[0] : nullptr, cyclesPerUnit * (double) fmTo3);
    out[0] = tickBank (banks[0], nullptr, 0.0);
}

void OscSection::tick (float fmTo2, float fmTo3, StereoOscs& out) noexcept
{
    if (oversampling == 1)
    {
        step (fmTo2, fmTo3, out);
        return;
    }

    StereoOscs first, second;
    step (fmTo2, fmTo3, first);
    step (fmTo2, fmTo3, second);

    for (size_t k = 0; k < banks.size(); ++k)
    {
        if (! banks[k].running)
        {
            out[k] = {};
            continue;
        }

        out[k].l = decimators[k][0].process (first[k].l, second[k].l);
        out[k].r = decimators[k][1].process (first[k].r, second[k].r);
    }
}

void OscSection::adoptFrom (const OscSection& other) noexcept
{
    for (size_t k = 0; k < banks.size(); ++k)
    {
        auto& d = banks[k];
        const auto& s = other.banks[k];

        stopBank (k);

        if (! s.running)
            continue;

        d.running = true;
        d.wave = s.wave;
        d.waveFrom = s.waveFrom;
        d.fadeRemaining = s.fadeRemaining * oversampling / other.oversampling;
        d.increment = s.increment;
        d.incrementStep = 0.0;
        d.pulseWidth = s.pulseWidth;
        d.widthStep = 0.0f;
        d.bandLimit = s.bandLimit;
        d.bandLimitTarget = s.bandLimitTarget;
        d.count = s.count;

        for (size_t c = 0; c < d.copies.size(); ++c)
        {
            auto& dc = d.copies[c];
            const auto& sc = s.copies[c];

            dc.active = sc.active;
            dc.ratio = sc.ratio;
            dc.ratioStep = 0.0;
            dc.gainL = sc.gainL;
            dc.gainR = sc.gainR;
            dc.targetL = sc.targetL;
            dc.targetR = sc.targetR;

            if (sc.active)
                dc.osc.jumpTo (sc.osc.getPhase());
        }
    }
}

bool OscSection::needsStereo() const noexcept
{
    for (const auto& b : banks)
    {
        if (! b.running)
            continue;

        for (const auto& cp : b.copies)
            if (cp.active && (cp.gainL != cp.gainR || cp.targetL != cp.targetR))
                return true;
    }

    return false;
}

} // namespace scratchpad::engine
