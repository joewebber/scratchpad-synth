#include "EngineRig.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace enginetest;
using Catch::Approx;
using engine::ModDest;
using engine::ModSource;
using engine::index;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** Oscillator 1 as a sine at -6 dB: half scale, well clear of the output limiter's knee, so what is
        measured is the modulation and not the limiter. */
    constexpr float quietScale = 0.5011872f;

    EngineParams quietSine()
    {
        auto p = sineParams();
        p.osc[0].levelDb = -6.0f;
        return p;
    }

    EngineParams withConnection (EngineParams p, size_t slot, ModSource source, ModDest dest, float amount)
    {
        p.mod[slot] = { source, dest, amount };
        return p;
    }

    EngineParams withLfo (EngineParams p, size_t lfo, dsp::LfoShape shape, float hz)
    {
        p.lfo[lfo].enabled = true;
        p.lfo[lfo].shape = shape;
        p.lfo[lfo].rateHz = hz;
        return p;
    }

    /** The newest played voice's source values, as the editor's live dots see them. */
    float live (Rig& rig, ModSource s)
    {
        return rig.engine.getLiveModulation().source[index (s)].load();
    }

    /** Lowest and highest frequency over successive short windows of a sine, to follow a vibrato. */
    std::pair<double, double> hzRange (const std::vector<float>& x)
    {
        double lo = 1.0e9, hi = 0.0;
        const size_t window = 600; // five cycles of A4

        for (size_t start = 0; start + window < x.size(); start += 200)
        {
            const double hz = measure (x, start, window).hz;
            lo = std::min (lo, hz);
            hi = std::max (hi, hz);
        }

        return { lo, hi };
    }

    /** Rising zero crossings of the LFO 1 live value, read every 32 samples, over `seconds`. */
    int lfoCycles (Rig& rig, double seconds)
    {
        int crossings = 0;
        float last = live (rig, ModSource::lfo1);

        for (int i = 0; i < (int) (seconds * sampleRate / 32.0); ++i)
        {
            rig.run (32);
            const float v = live (rig, ModSource::lfo1);
            crossings += last < 0.0f && v >= 0.0f;
            last = v;
        }

        return crossings;
    }
}

TEST_CASE ("Each LFO shape starts at its documented value and stays inside -1..+1", "[lfo]")
{
    using dsp::LfoShape;

    const struct { LfoShape shape; float start; } starts[] = {
        { LfoShape::sine, 0.0f }, { LfoShape::triangle, 0.0f }, { LfoShape::sawUp, -1.0f },
        { LfoShape::sawDown, 1.0f }, { LfoShape::square, 1.0f }
    };

    for (const auto& s : starts)
    {
        dsp::Lfo lfo;
        lfo.setSampleRate (48000.0);
        lfo.setShape (s.shape);
        lfo.setRate (3.0);
        lfo.reset (0.0);
        REQUIRE (lfo.value() == Approx (s.start).margin (1.0e-6));

        for (int i = 0; i < 48000; ++i)
            REQUIRE (std::abs (lfo.advance (1)) <= 1.0f);
    }

    // A quarter of the way round, sine and triangle are at their peak.
    for (const auto shape : { LfoShape::sine, LfoShape::triangle })
    {
        dsp::Lfo lfo;
        lfo.setSampleRate (48000.0);
        lfo.setShape (shape);
        lfo.setRate (1.0);
        lfo.reset (0.0);
        REQUIRE (lfo.advance (12000) == Approx (1.0f).margin (1.0e-4));
    }
}

TEST_CASE ("An LFO's rate is the number of cycles per second", "[lfo]")
{
    const int samples = 4 * 48000 - 1000; // stops short of a cycle boundary, so the count is not a rounding accident

    for (const double hz : { 0.5, 7.0, 40.0 })
    {
        dsp::Lfo lfo;
        lfo.setSampleRate (48000.0);
        lfo.setShape (dsp::LfoShape::sawUp);
        lfo.setRate (hz);
        lfo.reset (0.0);

        int wraps = 0;
        float last = lfo.value();

        for (int i = 0; i < samples; ++i)
        {
            const float v = lfo.advance (1);
            wraps += v < last;
            last = v;
        }

        REQUIRE (wraps == (int) std::floor (hz * (double) samples / 48000.0));
    }
}

TEST_CASE ("Sample and hold keeps one value for a cycle and draws a new one at the next", "[lfo]")
{
    dsp::Lfo lfo;
    lfo.setSampleRate (48000.0);
    lfo.seed (99);
    lfo.setShape (dsp::LfoShape::sampleHold);
    lfo.setRate (12.0); // 4000 samples a cycle
    lfo.reset (0.0);
    lfo.advance (500);

    std::vector<float> perCycle;

    for (int cycle = 0; cycle < 20; ++cycle)
    {
        const float held = lfo.value();

        for (int i = 0; i < 3; ++i)
            REQUIRE (lfo.advance (1000) == held);

        perCycle.push_back (held);
        lfo.advance (1000); // across the wrap
    }

    REQUIRE (std::adjacent_find (perCycle.begin(), perCycle.end()) == perCycle.end());
    REQUIRE (*std::min_element (perCycle.begin(), perCycle.end()) < -0.3f);
    REQUIRE (*std::max_element (perCycle.begin(), perCycle.end()) > 0.3f);
}

TEST_CASE ("Connections add up per destination and ignore sources that read zero", "[modulation]")
{
    engine::ModConnections c;
    c[0] = { ModSource::lfo1, ModDest::cutoff, 0.5f };
    c[1] = { ModSource::filterEnv, ModDest::cutoff, -0.25f };
    c[2] = { ModSource::velocity, ModDest::pan, 1.0f };
    c[3] = { ModSource::none, ModDest::resonance, 1.0f };
    c[4] = { ModSource::lfo2, ModDest::none, 1.0f };

    engine::ModSourceValues v {};
    v[index (ModSource::lfo1)] = -0.5f;
    v[index (ModSource::filterEnv)] = 0.8f;

    const auto sums = engine::accumulate (c, v);
    REQUIRE (sums[index (ModDest::cutoff)] == Approx (0.5f * -0.5f + -0.25f * 0.8f));
    REQUIRE (sums[index (ModDest::pan)] == 0.0f);
    REQUIRE (sums[index (ModDest::resonance)] == 0.0f);
}

TEST_CASE ("An LFO on pitch bends it by +-24 semitones at 100%, scaled by the amount", "[modulation][pitch]")
{
    for (const float amount : { 1.0f, 0.5f })
    {
        auto p = withConnection (withLfo (sineParams(), 0, dsp::LfoShape::sine, 3.0f), 0, ModSource::lfo1, ModDest::pitchAll, amount);
        Rig rig (p);
        rig.noteOn (69);
        rig.run (4800);
        const auto x = rig.run (48000);

        const auto [lo, hi] = hzRange (x);
        const double span = 24.0 * amount;
        REQUIRE (hi == Approx (440.0 * std::exp2 (span / 12.0)).epsilon (0.01));
        REQUIRE (lo == Approx (440.0 * std::exp2 (-span / 12.0)).epsilon (0.01));
    }
}

TEST_CASE ("A connection from an LFO that is not in the patch does nothing", "[modulation]")
{
    auto absent = withConnection (sineParams(), 0, ModSource::lfo1, ModDest::pitchAll, 1.0f);
    absent.lfo[0].rateHz = 3.0f; // present = false

    Rig rig (absent);
    rig.noteOn (69);
    rig.run (4800);
    const auto x = rig.run (24000);

    const auto [lo, hi] = hzRange (x);
    REQUIRE (lo == Approx (440.0).epsilon (0.001));
    REQUIRE (hi == Approx (440.0).epsilon (0.001));
}

TEST_CASE ("A connection to one oscillator's pitch moves only that oscillator", "[modulation][pitch]")
{
    auto p = silentParams();
    p.osc[1].enabled = true;
    p.osc[1].wave = dsp::Wave::sine;
    p.osc[1].levelDb = 0.0f;

    const auto settledHz = [&] (ModDest dest)
    {
        Rig rig (withConnection (p, 0, ModSource::modWheel, dest, 1.0f));
        rig.midi (juce::MidiMessage::controllerEvent (1, 1, 127));
        rig.noteOn (69);
        rig.run (9600);
        return measureHz (rig.run (9600));
    };

    REQUIRE (settledHz (ModDest::pitch2) == Approx (440.0 * std::exp2 (24.0 / 12.0)).epsilon (0.001));
    REQUIRE (settledHz (ModDest::pitchAll) == Approx (440.0 * std::exp2 (24.0 / 12.0)).epsilon (0.001));
    REQUIRE (settledHz (ModDest::pitch3) == Approx (440.0).epsilon (0.001));
    REQUIRE (settledHz (ModDest::pitch1) == Approx (440.0).epsilon (0.001)); // osc 1 is silent here
}

TEST_CASE ("The mod wheel pans a note: balance, not a level change at the centre", "[modulation][pan]")
{
    const auto levels = [] (float amount, juce::uint8 wheel)
    {
        Rig rig (withConnection (quietSine(), 0, ModSource::modWheel, ModDest::pan, amount));
        rig.midi (juce::MidiMessage::controllerEvent (1, 1, wheel));
        rig.noteOn (69);
        rig.runStereo (9600);
        const auto [l, r] = rig.runStereo (4800);
        return std::pair { (float) rms (l, 0, l.size()), (float) rms (r, 0, r.size()) };
    };

    const auto centre = levels (1.0f, 0);
    REQUIRE (centre.first == Approx (centre.second).epsilon (0.001));
    REQUIRE (centre.first == Approx (0.7071f * quietScale).epsilon (0.01)); // a half-scale sine

    const auto right = levels (1.0f, 127);
    REQUIRE (right.first < 0.001f);
    REQUIRE (right.second == Approx (centre.second).epsilon (0.01));

    const auto left = levels (-1.0f, 127);
    REQUIRE (left.second < 0.001f);
    REQUIRE (left.first == Approx (centre.first).epsilon (0.01));

    const auto half = levels (0.5f, 127);
    REQUIRE (half.first == Approx (0.5f * centre.first).epsilon (0.02));
}

TEST_CASE ("Moving the mod wheel across a pan never steps", "[modulation][pan][clicks]")
{
    Rig rig (withConnection (sineParams(), 0, ModSource::modWheel, ModDest::pan, 1.0f));
    rig.noteOn (69);
    rig.run (4800);
    rig.midi (juce::MidiMessage::controllerEvent (1, 1, 127));
    const auto [l, r] = rig.runStereo (4800);

    // A 440 Hz full-scale sine moves 0.058 per sample by itself.
    REQUIRE (maxStep (l) < 0.07f);
    REQUIRE (maxStep (r) < 0.07f);
}

TEST_CASE ("Velocity on amp level at -100% silences a hard note and halves a medium one", "[modulation][level]")
{
    const auto level = [] (juce::uint8 velocity)
    {
        Rig rig (withConnection (quietSine(), 0, ModSource::velocity, ModDest::ampLevel, -1.0f));
        rig.noteOn (69, velocity);
        rig.run (9600);
        return rms (rig.run (4800), 0, 4800);
    };

    const double unmodulated = 0.7071 * quietScale;
    REQUIRE (level (127) < 0.001);
    REQUIRE (level (64) == Approx (unmodulated * (1.0 - 64.0 / 127.0)).epsilon (0.02));
    REQUIRE (level (1) == Approx (unmodulated).epsilon (0.02));
}

TEST_CASE ("An oscillator level connection scales that oscillator, up to double", "[modulation][level]")
{
    const auto level = [] (float amount, juce::uint8 wheel)
    {
        auto p = sineParams();
        p.osc[0].levelDb = -12.0f;
        Rig rig (withConnection (p, 0, ModSource::modWheel, ModDest::level1, amount));
        rig.midi (juce::MidiMessage::controllerEvent (1, 1, wheel));
        rig.noteOn (69);
        rig.run (9600);
        return rms (rig.run (4800), 0, 4800);
    };

    const double base = level (1.0f, 0);
    REQUIRE (level (1.0f, 127) == Approx (2.0 * base).epsilon (0.02));
    REQUIRE (level (-1.0f, 127) < 0.001);
    REQUIRE (level (-0.5f, 127) == Approx (0.5 * base).epsilon (0.02));
}

TEST_CASE ("A pulse-width connection moves the duty cycle", "[modulation][pulse]")
{
    const auto mean = [] (juce::uint8 wheel)
    {
        auto p = quietSine();
        p.osc[0].wave = dsp::Wave::pulse;
        p.osc[0].pulseWidth = 0.5f;
        Rig rig (withConnection (p, 0, ModSource::modWheel, ModDest::pulseWidth1, 1.0f));
        rig.midi (juce::MidiMessage::controllerEvent (1, 1, wheel));
        rig.noteOn (57);
        rig.run (9600);
        const auto x = rig.run (4800); // an exact number of cycles of 220 Hz

        double sum = 0.0;
        for (float s : x)
            sum += s;
        return std::abs (sum / (double) x.size());
    };

    REQUIRE (mean (0) < 0.05 * quietScale);
    REQUIRE (mean (127) > 0.8 * quietScale); // width 0.5 + 0.45: 2 * 0.95 - 1 = 0.9
}

TEST_CASE ("Velocity opens the filter: +100% on cutoff is six octaves", "[modulation][filter]")
{
    auto p = sineParams();
    p.osc[0].wave = dsp::Wave::saw;
    p.filter.enabled = true;
    p.filter.mode = dsp::FilterMode::lp24;
    p.filter.cutoffHz = 500.0f;
    p.filter.resonance = 0.0f;

    const auto brightness = [&] (float amount, juce::uint8 velocity)
    {
        Rig rig (withConnection (p, 0, ModSource::velocity, ModDest::cutoff, amount));
        rig.noteOn (57, velocity);
        rig.run (9600);
        return bandLevel (rig.run (16384), 4000.0, 20000.0);
    };

    const double closed = brightness (1.0f, 1);   // velocity ~0: cutoff stays at 500 Hz
    const double opened = brightness (1.0f, 127); // 500 Hz * 2^6: wide open
    const double shut = brightness (-1.0f, 127);  // 500 Hz / 2^6

    REQUIRE (opened > 20.0 * closed);
    REQUIRE (shut < closed);
}

TEST_CASE ("A retriggering LFO starts every note at the top of its cycle; a free-running one is shared", "[modulation][lfo]")
{
    const auto lfoValues = [] (bool retrigger)
    {
        auto p = withLfo (silentParams(), 0, dsp::LfoShape::sine, 2.0f);
        p.lfo[0].retrigger = retrigger;

        Rig rig (p);
        rig.noteOn (60);
        rig.run (4800);   // 100 ms
        rig.noteOn (64);
        rig.run (1200);   // 25 ms more

        std::vector<float> values (2);
        for (const auto& v : rig.engine.getAllocator().getVoices())
        {
            if (v.state == engine::Voice::State::idle)
                continue;
            values[v.note == 60 ? 0 : 1] = v.getModSources()[index (ModSource::lfo1)];
        }
        return values;
    };

    const auto sine = [] (double seconds) { return (float) std::sin (2.0 * pi * 2.0 * seconds); };

    const auto retriggered = lfoValues (true);
    REQUIRE (retriggered[0] == Approx (sine (0.125)).margin (0.03)); // the old note is 125 ms into its cycle
    REQUIRE (retriggered[1] == Approx (sine (0.025)).margin (0.03)); // the new one only 25 ms

    const auto shared = lfoValues (false);
    REQUIRE (shared[0] == shared[1]);
    REQUIRE (shared[0] == Approx (sine (0.125)).margin (0.03)); // both follow the one LFO
}

TEST_CASE ("LFO fade-in raises the depth over the fade time after each note", "[modulation][lfo]")
{
    auto p = withLfo (silentParams(), 0, dsp::LfoShape::square, 0.5f); // +1 for the first second
    p.lfo[0].fadeSeconds = 0.5f;

    Rig rig (p);
    rig.noteOn (60);
    rig.run (12000); // 250 ms
    REQUIRE (live (rig, ModSource::lfo1) == Approx (0.5f).margin (0.03));
    rig.run (9600);  // 450 ms
    REQUIRE (live (rig, ModSource::lfo1) == Approx (0.9f).margin (0.04));
    rig.run (4800);  // 550 ms: past the fade, still in the first half of the square
    REQUIRE (live (rig, ModSource::lfo1) == Approx (1.0f).margin (0.001));
}

TEST_CASE ("The mod wheel can speed an LFO up by three octaves", "[modulation][lfo]")
{
    const auto cycles = [] (juce::uint8 wheel)
    {
        auto p = withLfo (silentParams(), 0, dsp::LfoShape::sine, 2.0f);
        p = withConnection (p, 0, ModSource::modWheel, ModDest::lfo1Rate, 1.0f);

        Rig rig (p);
        rig.midi (juce::MidiMessage::controllerEvent (1, 1, wheel));
        rig.run (4800);
        return lfoCycles (rig, 1.0);
    };

    REQUIRE (cycles (0) == 2);
    REQUIRE (cycles (127) == 16);
}

TEST_CASE ("An LFO can move another LFO's rate", "[modulation][lfo]")
{
    auto p = withLfo (silentParams(), 0, dsp::LfoShape::sine, 2.0f);
    p = withLfo (p, 1, dsp::LfoShape::square, 0.5f);
    p = withConnection (p, 0, ModSource::lfo2, ModDest::lfo1Rate, 1.0f);

    Rig rig (p);
    rig.run (4800);

    // LFO 2 is +1 for its first second: LFO 1 runs three octaves fast, at 16 Hz.
    REQUIRE (lfoCycles (rig, 0.5) >= 7); // 0.5 s at 16 Hz is 8 cycles
}

TEST_CASE ("The key tracks note number: one at C7, minus one at C-1, zero at middle C", "[modulation]")
{
    const auto key = [] (int note)
    {
        Rig rig (sineParams());
        rig.noteOn (note);
        rig.run (2048);
        return live (rig, ModSource::key);
    };

    REQUIRE (key (60) == 0.0f);
    REQUIRE (key (120) == Approx (1.0f));
    REQUIRE (key (0) == Approx (-1.0f));
}

TEST_CASE ("Envelope sources read the envelope when its module is present, and zero when it is not", "[modulation][envelope]")
{
    auto p = sineParams();
    p.filterEnv.attack = 0.001f;
    p.filterEnv.decay = 10.0f;
    p.filterEnv.sustain = 1.0f;

    {
        Rig rig (p);
        rig.noteOn (60);
        rig.run (4800);
        REQUIRE (live (rig, ModSource::filterEnv) == 0.0f);
    }

    p.filterEnv.enabled = true;
    Rig rig (p);
    rig.noteOn (60);
    rig.run (4800);
    REQUIRE (live (rig, ModSource::filterEnv) > 0.9f);
}

TEST_CASE ("Retriggering a mono voice restarts its retriggering LFO", "[modulation][lfo]")
{
    auto p = withLfo (sineParams(), 0, dsp::LfoShape::sine, 5.0f);
    p.lfo[0].retrigger = true;
    p.voicing.mode = engine::VoiceMode::mono;

    Rig rig (p);
    rig.noteOn (60);
    rig.run (4800);
    rig.noteOn (62); // mono: the same voice, new attack
    rig.run (320);

    // 6.7 ms into the new note the LFO is a thirtieth of a cycle along: near zero.
    REQUIRE (std::abs (live (rig, ModSource::lfo1)) < 0.25f);
}
