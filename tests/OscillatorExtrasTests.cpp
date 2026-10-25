#include "EngineRig.h"

#include "dsp/Oscillator.h"
#include "engine/OscSection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

/** M5's oscillator interactions (hard sync, ring modulation, FM), unison and the anti-aliasing switch,
    tested against what they are supposed to do to the spectrum and the waveform. */

using namespace enginetest;
using scratchpad::dsp::Wave;
using scratchpad::engine::OscSection;

namespace
{
    using Stereo = std::pair<std::vector<float>, std::vector<float>>;

    constexpr size_t window = 32768; // a power of two, for the FFT: 0.68 s, 1.46 Hz per bin
    constexpr int note220 = 57;      // A3
    constexpr double f1 = 220.0;

    /** Oscillator 2's frequency in the pair patches: 7.4 semitones above oscillator 1, which keeps
        the two frequencies and their harmonics apart. */
    const double f2 = f1 * std::exp2 (7.4 / 12.0);

    /** Plays `note`, drops `warm` samples (the gate and the smoothers settle) and returns `count` samples. */
    Stereo play (const EngineParams& p, int note, size_t warm, size_t count)
    {
        Rig rig (p);
        rig.noteOn (note);
        rig.runStereo ((int) warm);
        return rig.runStereo ((int) count);
    }

    /** Oscillator 1 silent (level -inf) as the master, oscillator 2 at full level, 7.4 semitones up. */
    EngineParams pairParams (Wave master, Wave second)
    {
        EngineParams p = silentParams();
        p.osc[0].wave = master;
        p.osc[1].enabled = true;
        p.osc[1].wave = second;
        p.osc[1].levelDb = 0.0f;
        p.osc[1].semitone = 7;
        p.osc[1].fineCents = 40.0f;
        return p;
    }

    struct Spectrum
    {
        explicit Spectrum (const std::vector<float>& x)
            : mag (testutil::hannSpectrum (x)), binHz (sampleRate / (double) x.size())
        {
        }

        /** Energy between `lo` and `hi` Hz (arbitrary but consistent units). */
        double power (double lo, double hi) const
        {
            double sum = 0.0;
            for (size_t k = 0; k < mag.size(); ++k)
            {
                const double f = (double) k * binHz;
                if (f >= lo && f <= hi)
                    sum += mag[k] * mag[k];
            }
            return sum;
        }

        double around (double hz, double halfWidth) const { return power (hz - halfWidth, hz + halfWidth); }

        /** The share of the energy that sits within `halfWidth` of a multiple of `f0`. */
        double harmonicShare (double f0, double halfWidth) const
        {
            double inside = 0.0;
            for (int h = 1; (double) h * f0 < 20000.0; ++h)
                inside += around ((double) h * f0, halfWidth);

            return inside / power (5.0, 24000.0);
        }

        std::vector<double> mag;
        double binHz;
    };

    /** Energy off the harmonic grid relative to the fundamental's, in dB (the measure `OscillatorTests` uses). */
    double aliasDb (const std::vector<float>& x, double hz)
    {
        const auto spectrum = testutil::hannSpectrum (x);
        const double binHz = sampleRate / (double) x.size();
        const int fundamentalBin = (int) std::lround (hz / binHz);

        double fundamental = 0.0;
        for (int b = fundamentalBin - 3; b <= fundamentalBin + 3; ++b)
            fundamental = std::max (fundamental, spectrum[(size_t) b]);

        double worst = 0.0;
        for (size_t b = 16; b < spectrum.size(); ++b)
        {
            bool nearHarmonic = false;
            for (int h = 1; (double) h * hz < sampleRate * 0.5 + 2.0 * binHz; ++h)
            {
                if (std::abs ((double) b * binHz - (double) h * hz) < 16.0 * binHz)
                {
                    nearHarmonic = true;
                    break;
                }
            }

            if (! nearHarmonic)
                worst = std::max (worst, spectrum[b]);
        }

        return 20.0 * std::log10 (worst / fundamental + 1.0e-12);
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            ab += (double) a[i] * (double) b[i];
            aa += (double) a[i] * (double) a[i];
            bb += (double) b[i] * (double) b[i];
        }
        return ab / std::sqrt (aa * bb);
    }

    /** RMS of the side signal over the RMS of the mid signal. */
    double sideToMid (const Stereo& s)
    {
        double side = 0.0, mid = 0.0;
        for (size_t i = 0; i < s.first.size(); ++i)
        {
            const double l = s.first[i], r = s.second[i];
            side += (l - r) * (l - r);
            mid += (l + r) * (l + r);
        }
        return std::sqrt (side / mid);
    }

    double meanSquare (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (float s : x)
            sum += (double) s * (double) s;
        return sum / (double) x.size();
    }
}

TEST_CASE ("At the defaults the engine plays exactly what a bare oscillator does", "[oscillator][unison]")
{
    const auto wave = GENERATE (Wave::sine, Wave::triangle, Wave::saw, Wave::pulse);

    // A quiet level: the band-limited saw and pulse overshoot 1.0 by up to 16 %, and at full level the
    // always-on safety limiter (it starts to act at -0.9 dBFS) would legitimately turn them down.
    constexpr float levelDb = -12.0f;
    const float level = std::pow (10.0f, levelDb / 20.0f);

    EngineParams p = sineParams();
    p.osc[0].wave = wave;
    p.osc[0].levelDb = levelDb;
    Rig rig (p);
    rig.noteOn (note220);
    const auto out = rig.run (4800);

    scratchpad::dsp::Oscillator reference;
    reference.setIncrement (440.0 * std::exp2 ((57.0 - 69.0) / 12.0) / sampleRate);
    reference.setPulseWidth (0.5);

    for (size_t n = 0; n < out.size(); ++n)
    {
        const float expected = reference.process (wave) * level;

        if (n >= 300) // the gate is fully open
        {
            INFO ("wave " << (int) wave << " sample " << n);
            REQUIRE (std::abs (out[n] - expected) < 1.0e-6f);
        }
    }
}

// --- Hard sync ---------------------------------------------------------------------------------

TEST_CASE ("A synced oscillator repeats at the master's period and loses its own pitch", "[sync]")
{
    auto syncedParams = pairParams (Wave::saw, Wave::saw);
    syncedParams.osc[1].sync = true;
    const auto freeParams = pairParams (Wave::saw, Wave::saw);

    const Spectrum synced (play (syncedParams, note220, 4800, window).first);
    const Spectrum free (play (freeParams, note220, 4800, window).first);

    // Synced: every bit of energy is on a multiple of oscillator 1's frequency.
    REQUIRE (synced.harmonicShare (f1, 8.0) > 0.97);

    // Free: oscillator 2 keeps its own pitch, which is not a multiple of oscillator 1's.
    REQUIRE (free.harmonicShare (f1, 8.0) < 0.4);

    // The slave's own fundamental is the tearing that sync removes: its band is empty when synced.
    REQUIRE (synced.around (f2, 8.0) < 1.0e-3 * free.around (f2, 8.0));
    REQUIRE (synced.around (f1, 8.0) > 100.0 * free.around (f1, 8.0));
}

TEST_CASE ("The sync restart is band-limited", "[sync][aliasing]")
{
    auto on = pairParams (Wave::saw, Wave::saw);
    on.osc[1].sync = true;
    auto off = on;
    off.osc[1].antiAlias = false;

    // Aliasing of a restart lands off the harmonic grid of the master.
    const double aliasedOn = 1.0 - Spectrum (play (on, note220, 4800, window).first).harmonicShare (f1, 8.0);
    const double aliasedOff = 1.0 - Spectrum (play (off, note220, 4800, window).first).harmonicShare (f1, 8.0);

    INFO ("off-grid share with anti-aliasing on " << aliasedOn << ", off " << aliasedOff);
    REQUIRE (aliasedOff > 1.0e-3);
    REQUIRE (aliasedOn < 0.5 * aliasedOff);
}

TEST_CASE ("Sync with a sine slave works and stays bounded", "[sync]")
{
    auto p = pairParams (Wave::saw, Wave::sine);
    p.osc[1].sync = true;
    const auto x = play (p, note220, 4800, window).first;

    REQUIRE (peak (x) < 1.3f);
    REQUIRE (peak (x) > 0.5f);
    REQUIRE (Spectrum (x).harmonicShare (f1, 8.0) > 0.97);
}

// --- Ring modulation ---------------------------------------------------------------------------

namespace
{
    EngineParams ringParams (float ringDb)
    {
        auto p = pairParams (Wave::sine, Wave::sine);
        p.osc[1].levelDb = engine::minLevelDb; // both oscillators are muted: only the ring source is heard
        p.ringLevelDb = ringDb;
        return p;
    }
}

TEST_CASE ("Ring modulation has the sum and difference frequencies and neither carrier", "[ring]")
{
    const Spectrum s (play (ringParams (0.0f), note220, 4800, window).first);

    const double sum = s.around (f1 + f2, 6.0);
    const double difference = s.around (f2 - f1, 6.0);

    REQUIRE (sum > 1.0e6);
    REQUIRE (difference > 0.5 * sum);
    REQUIRE (difference < 2.0 * sum);

    // 40 dB down or better; the product of two sines has no carrier terms at all.
    REQUIRE (s.around (f1, 6.0) < 1.0e-4 * sum);
    REQUIRE (s.around (f2, 6.0) < 1.0e-4 * sum);
}

TEST_CASE ("Ring modulation level follows its dB setting and -inf is silent", "[ring]")
{
    // Both settings stay below the safety limiter's knee (-0.9 dBFS): at 0 dB the product of two full-scale
    // sines touches 1.0 and the limiter would take the louder one down, which is not what is measured here.
    const Spectrum full (play (ringParams (-12.0f), note220, 4800, window).first);
    const Spectrum half (play (ringParams (-18.0206f), note220, 4800, window).first);

    // Energy (amplitude squared) falls by the square of the 6.02 dB amplitude ratio.
    REQUIRE (half.around (f1 + f2, 6.0) / full.around (f1 + f2, 6.0) == Catch::Approx (0.25).epsilon (0.02));

    REQUIRE (peak (play (ringParams (engine::minLevelDb), note220, 4800, 4800).first) == 0.0f);
}

TEST_CASE ("Ring modulation needs oscillators 1 and 2 both present", "[ring]")
{
    auto p = ringParams (0.0f);
    p.osc[1].enabled = false;
    REQUIRE (peak (play (p, note220, 4800, 4800).first) == 0.0f);
}

TEST_CASE ("Ring modulation adds to the oscillators that are audible", "[ring]")
{
    auto p = ringParams (-12.0f);
    p.osc[0].levelDb = -12.0f;
    p.osc[1].levelDb = -12.0f;
    const Spectrum s (play (p, note220, 4800, window).first);

    // The carriers are back, and so are the products.
    REQUIRE (s.around (f1, 6.0) > 2.0 * s.around (f1 + f2, 6.0));
    REQUIRE (s.around (f2, 6.0) > 2.0 * s.around (f1 + f2, 6.0));
    REQUIRE (s.around (f1 + f2, 6.0) > 1.0e4);
}

// --- FM ----------------------------------------------------------------------------------------

namespace
{
    /** Oscillator 1 (silent) modulates oscillator 2 (a sine at `f2`), both sines. */
    EngineParams fmParams (float amount)
    {
        auto p = pairParams (Wave::sine, Wave::sine);
        p.fmAmount = amount;
        return p;
    }

    constexpr double testIndex = 3.0; // radians of peak phase deviation
    const float testAmount = (float) (testIndex / OscSection::fmMaxIndex);
}

TEST_CASE ("FM adds sidebands at the carrier plus and minus multiples of the modulator", "[fm]")
{
    const Spectrum plain (play (fmParams (0.0f), note220, 4800, window).first);
    const Spectrum fm (play (fmParams (testAmount), note220, 4800, window).first);

    const double carrier = plain.around (f2, 6.0);
    REQUIRE (carrier > 1.0e6);

    // Nothing at the sideband positions without FM.
    REQUIRE (plain.around (f2 + f1, 6.0) < 1.0e-4 * carrier);
    REQUIRE (plain.around (f2 + 2.0 * f1, 6.0) < 1.0e-4 * carrier);

    // With a modulation index of 3 the sidebands follow the Bessel functions of the first kind:
    // J0(3) for the carrier, J1(3) and J2(3) for the first two pairs.
    const auto bessel = [] (double k) { return std::cyl_bessel_j (k, testIndex); };
    const auto relative = [&] (double hz) { return fm.around (hz, 6.0) / carrier; };

    const double j0 = bessel (0.0) * bessel (0.0);
    const double j1 = bessel (1.0) * bessel (1.0);
    const double j2 = bessel (2.0) * bessel (2.0);

    REQUIRE (relative (f2) == Catch::Approx (j0).epsilon (0.25));
    REQUIRE (relative (f2 + f1) == Catch::Approx (j1).epsilon (0.25));
    REQUIRE (relative (f2 - f1) == Catch::Approx (j1).epsilon (0.25));
    REQUIRE (relative (f2 + 2.0 * f1) == Catch::Approx (j2).epsilon (0.25));
    REQUIRE (relative (std::abs (f2 - 2.0 * f1)) == Catch::Approx (j2).epsilon (0.25)); // folds back through 0 Hz
}

TEST_CASE ("FM amount 0 is bit-identical to FM off, whatever the target", "[fm]")
{
    const auto off = fmParams (0.0f);
    auto both = off;
    both.fmTarget = scratchpad::engine::FmTarget::both;
    both.osc[2].enabled = true;
    both.osc[2].levelDb = engine::minLevelDb;
    auto third = off;
    third.fmTarget = scratchpad::engine::FmTarget::osc3;

    const auto reference = play (off, note220, 0, 9600);
    REQUIRE (play (both, note220, 0, 9600) == reference);
    REQUIRE (play (third, note220, 0, 9600) == reference);
}

TEST_CASE ("The oversampled pair arrives in step with the base-rate one", "[fm]")
{
    // An FM amount that is almost nothing runs the 2x section but changes the wave by almost nothing, so
    // the result must equal the plain render: same pitch, same phase, same delay, same level.
    Rig plain (fmParams (0.0f)), tiny (fmParams (0.0f));
    plain.noteOn (note220);
    tiny.noteOn (note220);

    const auto before = plain.runStereo (4800).first;
    REQUIRE (tiny.runStereo (4800).first == before); // nothing differs until FM is switched on

    tiny.set (fmParams (1.0e-6f));
    const auto reference = plain.runStereo (9600).first;
    const auto crossed = tiny.runStereo (9600).first;

    for (size_t n = 3000; n < reference.size(); ++n)
    {
        INFO ("sample " << n);
        REQUIRE (std::abs (crossed[n] - reference[n]) < 3.0e-3f);
    }

    // Back to no FM: the same again.
    tiny.set (fmParams (0.0f));
    const auto referenceAgain = plain.runStereo (9600).first;
    const auto backAgain = tiny.runStereo (9600).first;

    for (size_t n = 3000; n < referenceAgain.size(); ++n)
    {
        INFO ("sample " << n);
        REQUIRE (std::abs (backAgain[n] - referenceAgain[n]) < 3.0e-3f);
    }
}

TEST_CASE ("Turning FM on and off does not click or dip", "[fm][clicks]")
{
    Rig rig (fmParams (0.0f));
    rig.noteOn (note220);
    rig.runStereo (4800);

    std::vector<float> all;
    const auto append = [&] (const std::vector<float>& x) { all.insert (all.end(), x.begin(), x.end()); };

    rig.set (fmParams (testAmount));
    append (rig.runStereo (9600).first);
    rig.set (fmParams (0.0f));
    append (rig.runStereo (9600).first);
    rig.set (fmParams (testAmount));
    append (rig.runStereo (9600).first);

    // Steady FM at index 3 has an instantaneous frequency of at most f2 + 3 * f1, about 0.13 rad per sample.
    // A pop (a dropped decimator history, a misaligned crossfade) is several times that.
    const double steadyStep = 2.0 * 3.14159265 * (f2 + testIndex * f1) / sampleRate;
    REQUIRE (maxStep (all) < 1.15 * steadyStep);

    // A sine carrier keeps its amplitude whatever the modulation, so a crossfade that goes wrong dips.
    for (size_t from = 2000; from + 240 <= all.size(); from += 240)
    {
        const float p = peakOf (all, from, 240);
        INFO ("window at " << from);
        REQUIRE (p > 0.93f);
        REQUIRE (p < 1.06f);
    }
}

TEST_CASE ("FM into oscillator 3 and into both", "[fm]")
{
    auto p = fmParams (testAmount);
    p.osc[1].enabled = false;
    p.osc[2].enabled = true;
    p.osc[2].wave = Wave::sine;
    p.osc[2].levelDb = 0.0f;
    p.osc[2].semitone = 7;
    p.osc[2].fineCents = 40.0f;

    // Target oscillator 2, which is absent: oscillator 3 is not modulated.
    p.fmTarget = scratchpad::engine::FmTarget::osc2;
    const Spectrum unmodulated (play (p, note220, 4800, window).first);
    REQUIRE (unmodulated.around (f2 + f1, 6.0) < 1.0e-4 * unmodulated.around (f2, 6.0));

    p.fmTarget = scratchpad::engine::FmTarget::osc3;
    const Spectrum modulated (play (p, note220, 4800, window).first);
    REQUIRE (modulated.around (f2 + f1, 6.0) > 0.05 * unmodulated.around (f2, 6.0));

    p.fmTarget = scratchpad::engine::FmTarget::both;
    const Spectrum both (play (p, note220, 4800, window).first);
    REQUIRE (both.around (f2 + f1, 6.0) > 0.05 * unmodulated.around (f2, 6.0));
}

// --- Unison ------------------------------------------------------------------------------------

namespace
{
    EngineParams unisonParams (int copies, float detuneCents, float spread, Wave wave = Wave::saw)
    {
        EngineParams p = sineParams();
        p.osc[0].wave = wave;
        p.osc[0].unison = copies;
        p.osc[0].unisonDetuneCents = detuneCents;
        p.osc[0].unisonSpread = spread;
        return p;
    }
}

TEST_CASE ("Unison 7 is wide; a single copy is not", "[unison]")
{
    const auto one = play (unisonParams (1, 20.0f, 1.0f), note220, 4800, 16384);
    const auto seven = play (unisonParams (7, 20.0f, 1.0f), note220, 4800, 16384);

    REQUIRE (one.first == one.second);
    REQUIRE (sideToMid (one) == 0.0);

    REQUIRE (seven.first != seven.second);
    REQUIRE (sideToMid (seven) > 0.15);
    REQUIRE (correlation (seven.first, seven.second) < 0.95);
}

TEST_CASE ("Unison with no spread, or no detune and no spread, is mono", "[unison]")
{
    // Detuned but not spread: the copies share the middle, left and right are identical.
    const auto detuned = play (unisonParams (7, 20.0f, 0.0f), note220, 4800, 9600);
    REQUIRE (detuned.first == detuned.second);

    // Not detuned and not spread.
    const auto stacked = play (unisonParams (7, 0.0f, 0.0f), note220, 4800, 9600);
    REQUIRE (stacked.first == stacked.second);

    // With the filter in, which is stereo only while the copies are.
    auto filtered = unisonParams (7, 20.0f, 0.0f);
    filtered.filter.enabled = true;
    const auto through = play (filtered, note220, 4800, 9600);
    REQUIRE (through.first == through.second);

    filtered.osc[0].unisonSpread = 1.0f;
    const auto wide = play (filtered, note220, 4800, 9600);
    REQUIRE (wide.first != wide.second);
    REQUIRE (peak (wide.first) < 4.0f);
    REQUIRE (std::isfinite (rms (wide.second, 0, wide.second.size())));
}

TEST_CASE ("Unison detune spreads the copies in pitch; without it they stay on one line", "[unison]")
{
    const Spectrum stacked (play (unisonParams (7, 0.0f, 1.0f), note220, 4800, window).first);
    const Spectrum spread (play (unisonParams (7, 100.0f, 1.0f), note220, 4800, window).first);

    REQUIRE (stacked.harmonicShare (f1, 3.0) > 0.97);
    REQUIRE (spread.harmonicShare (f1, 3.0) < 0.6);
}

TEST_CASE ("Unison copies sit symmetrically around the note", "[unison]")
{
    // Three copies at +-50 cents and one in the middle: the spectrum has a line at each, and the
    // outer lines are as far from the note as each other (in cents).
    // Played quietly: three sines in phase add up to well over the safety limiter's knee, and a limiter
    // that works on the peaks smears sidebands across the very gaps this test looks into.
    auto p = unisonParams (3, 50.0f, 0.0f, Wave::sine);
    p.osc[0].levelDb = -18.0f;
    const Spectrum s (play (p, note220, 4800, window).first);

    const double up = f1 * std::exp2 (50.0 / 1200.0);
    const double down = f1 * std::exp2 (-50.0 / 1200.0);
    const double middle = s.around (f1, 2.5);
    const double above = s.around (up, 2.5);
    const double below = s.around (down, 2.5);

    REQUIRE (middle > 0.0);
    REQUIRE (above / middle == Catch::Approx (1.0).epsilon (0.5));
    REQUIRE (below / middle == Catch::Approx (1.0).epsilon (0.5));
    REQUIRE (above / below == Catch::Approx (1.0).epsilon (0.2));

    // Nothing at +-100 cents.
    REQUIRE (s.around (f1 * std::exp2 (100.0 / 1200.0), 2.5) < 1.0e-3 * middle);
}

TEST_CASE ("Unison keeps the loudness about the same", "[unison]")
{
    const auto one = play (unisonParams (1, 20.0f, 1.0f), note220, 4800, 96000);
    const auto seven = play (unisonParams (7, 20.0f, 1.0f), note220, 4800, 96000);

    const double single = meanSquare (one.first) + meanSquare (one.second);
    const double stack = meanSquare (seven.first) + meanSquare (seven.second);

    REQUIRE (stack / single > 0.6);
    REQUIRE (stack / single < 1.7);
}

TEST_CASE ("Unison copies start at random phases, the same ones on every run", "[unison]")
{
    const auto a = play (unisonParams (5, 0.0f, 1.0f), note220, 0, 2400);
    const auto b = play (unisonParams (5, 0.0f, 1.0f), note220, 0, 2400);
    REQUIRE (a == b); // reproducible

    // The start is not the single-oscillator start: the copies do not all begin at phase 0 together.
    const auto single = play (unisonParams (1, 0.0f, 1.0f), note220, 0, 2400);
    REQUIRE (a.first != single.first);
}

TEST_CASE ("Changing the unison count, detune and spread does not click", "[unison][clicks]")
{
    EngineParams p = unisonParams (1, 20.0f, 1.0f, Wave::sine);
    p.filter.enabled = true;
    p.filter.mode = scratchpad::dsp::FilterMode::lp12;
    p.filter.cutoffHz = 8000.0f;
    p.filter.resonance = 0.0f;

    Rig rig (p);
    rig.noteOn (45); // 110 Hz
    rig.runStereo (4800);

    std::vector<float> left, right;
    const auto run = [&] (const EngineParams& q, int samples)
    {
        rig.set (q);
        const auto s = rig.runStereo (samples);
        left.insert (left.end(), s.first.begin(), s.first.end());
        right.insert (right.end(), s.second.begin(), s.second.end());
    };

    run (p, 2400);
    p.osc[0].unison = 7;
    run (p, 9600);
    p.osc[0].unison = 3;
    p.osc[0].unisonDetuneCents = 60.0f;
    run (p, 9600);
    p.osc[0].unisonSpread = 0.0f;
    run (p, 9600);
    p.osc[0].unison = 1;
    run (p, 9600);

    // Seven sines at 110 Hz move at most 7 * 0.0144 * 0.53 per sample (about 0.054) when all in step;
    // a copy that switched on or off at a random phase would jump by up to 0.5.
    REQUIRE (maxStep (left) < 0.06f);
    REQUIRE (maxStep (right) < 0.06f);
}

// --- Anti-aliasing switch ------------------------------------------------------------------------

TEST_CASE ("Anti-aliasing off aliases far more than on", "[aliasing]")
{
    constexpr size_t n = 16384;
    const double hz = testutil::midiToHz (96);

    auto on = sineParams();
    on.osc[0].wave = Wave::saw;
    auto off = on;
    off.osc[0].antiAlias = false;

    const double dbOn = aliasDb (play (on, 96, 4800, n).first, hz);
    const double dbOff = aliasDb (play (off, 96, 4800, n).first, hz);

    INFO ("alias level: on " << dbOn << " dB, off " << dbOff << " dB");
    REQUIRE (dbOn < -55.0);
    REQUIRE (dbOff > -40.0);
    REQUIRE (dbOff - dbOn > 25.0);
}

TEST_CASE ("Every wave aliases with anti-aliasing off", "[aliasing]")
{
    const auto wave = GENERATE (Wave::saw, Wave::pulse, Wave::triangle);
    constexpr size_t n = 16384;
    const double hz = testutil::midiToHz (96);

    auto on = sineParams();
    on.osc[0].wave = wave;
    auto off = on;
    off.osc[0].antiAlias = false;

    const double dbOn = aliasDb (play (on, 96, 4800, n).first, hz);
    const double dbOff = aliasDb (play (off, 96, 4800, n).first, hz);

    INFO ("wave " << (int) wave << ": on " << dbOn << " dB, off " << dbOff << " dB");
    REQUIRE (dbOff - dbOn > 10.0);
}

TEST_CASE ("Anti-aliasing off plays the naive wave, delayed like the band-limited one", "[aliasing]")
{
    // Quiet enough to stay below the safety limiter's knee: a naive saw reaches 1.0.
    constexpr float levelDb = -12.0f;
    const double level = std::pow (10.0, (double) levelDb / 20.0);

    auto p = sineParams();
    p.osc[0].wave = Wave::saw;
    p.osc[0].levelDb = levelDb;
    p.osc[0].antiAlias = false;

    Rig rig (p);
    rig.noteOn (note220);
    const auto out = rig.run (2400);

    const double dt = f1 / sampleRate;
    for (size_t n = 400; n < out.size(); ++n)
    {
        const double phase = std::fmod ((double) (n - (size_t) scratchpad::dsp::Oscillator::latencySamples) * dt, 1.0);
        const double expected = (2.0 * phase - 1.0) * level;

        // A sample on the jump can land either side of it; every other sample is exact.
        if (std::abs (phase) < 1.0e-6 || std::abs (phase - 1.0) < 1.0e-6)
            continue;

        INFO ("sample " << n);
        REQUIRE (std::abs ((double) out[n] - expected) < 1.0e-4);
    }
}

TEST_CASE ("Switching anti-aliasing does not click", "[aliasing][clicks]")
{
    // A triangle is continuous either way, so any step beyond its slope is the switch.
    auto p = sineParams();
    p.osc[0].wave = Wave::triangle;

    Rig rig (p);
    rig.noteOn (45); // 110 Hz
    rig.runStereo (4800);

    std::vector<float> x;
    const auto run = [&] (bool antiAlias, int samples)
    {
        p.osc[0].antiAlias = antiAlias;
        rig.set (p);
        const auto s = rig.runStereo (samples).first;
        x.insert (x.end(), s.begin(), s.end());
    };

    run (true, 2400);
    run (false, 9600);
    run (true, 9600);
    run (false, 4800);

    const double slope = 4.0 * 110.0 / sampleRate; // per sample, at the triangle's steepest
    REQUIRE (maxStep (x) < 1.5 * slope);

    // And the switch is a crossfade, not a jump: a saw's steps are the same size before, during and after.
    auto saw = sineParams();
    saw.osc[0].wave = Wave::saw;
    Rig sawRig (saw);
    sawRig.noteOn (45);
    sawRig.runStereo (4800);

    std::vector<float> y;
    for (bool antiAlias : { true, false, true })
    {
        saw.osc[0].antiAlias = antiAlias;
        sawRig.set (saw);
        const auto s = sawRig.runStereo (4800).first;
        y.insert (y.end(), s.begin(), s.end());
    }

    // A naive saw falls by about 2 in one sample; the band-limited one takes several samples.
    // What must not happen is a step bigger than the naive wave's own fall.
    REQUIRE (maxStep (y) < 2.1f);
    REQUIRE (peak (y) < 1.3f);
}
