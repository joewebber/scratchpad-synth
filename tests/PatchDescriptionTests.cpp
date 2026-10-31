#include "plugin/PatchDescription.h"

#include <catch2/catch_test_macros.hpp>

using namespace scratchpad;
using engine::EngineParams;
using engine::ModDest;
using engine::ModSource;
using plugin::describe;

namespace
{
    EngineParams oneSaw()
    {
        EngineParams p;
        p.osc[0].enabled = true;
        p.osc[0].wave = dsp::Wave::saw;
        return p;
    }

    /** Osc 1..3 with these waves, all at the same pitch. */
    EngineParams waves (std::initializer_list<dsp::Wave> list)
    {
        EngineParams p;
        size_t i = 0;
        for (const auto w : list)
        {
            p.osc[i].enabled = true;
            p.osc[i].wave = w;
            ++i;
        }
        return p;
    }

    std::string text (const EngineParams& p)
    {
        return describe (p).toStdString();
    }

    /** The sentence about the sources alone: everything before the first full stop. */
    std::string firstSentence (const EngineParams& p)
    {
        const auto t = text (p);
        const auto stop = t.find (". ");
        return stop != std::string::npos ? t.substr (0, stop) : t.substr (0, t.size() - (t.back() == '.' ? 1 : 0));
    }

    EngineParams twoSaws()
    {
        auto p = oneSaw();
        p.osc[1].enabled = true;
        p.osc[1].wave = dsp::Wave::saw;
        p.osc[1].fineCents = 7.0f;
        return p;
    }
}

TEST_CASE ("describe: nothing and a lone wave", "[describe]")
{
    REQUIRE (text (EngineParams {}) == "Nothing is making sound yet.");
    REQUIRE (text (oneSaw()) == "A saw wave. The keys switch it straight on and off.");

    auto noiseOnly = EngineParams {};
    noiseOnly.noise.enabled = true;
    noiseOnly.noise.pink = true;
    REQUIRE (text (noiseOnly).rfind ("A little pink noise.", 0) == 0);
}

TEST_CASE ("describe: counts agree with their nouns and use words for two and three", "[describe]")
{
    using dsp::Wave;
    REQUIRE (firstSentence (waves ({ Wave::saw })) == "A saw wave");
    REQUIRE (firstSentence (waves ({ Wave::sine })) == "A sine wave");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::saw })) == "Two saws");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::saw, Wave::saw })) == "Three saws");
    REQUIRE (firstSentence (waves ({ Wave::sine, Wave::sine, Wave::sine })) == "Three sines");
    REQUIRE (firstSentence (waves ({ Wave::pulse, Wave::pulse })) == "Two pulses");
    REQUIRE (firstSentence (waves ({ Wave::triangle, Wave::triangle })) == "Two triangles");
}

TEST_CASE ("describe: different waves are listed A, B and C without stray commas", "[describe]")
{
    using dsp::Wave;
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::sine })) == "A saw and a sine");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::saw, Wave::sine })) == "Two saws and a sine");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::sine, Wave::saw })) == "Two saws and a sine");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::pulse, Wave::triangle })) == "A saw, a pulse and a triangle");
    REQUIRE (firstSentence (waves ({ Wave::sine, Wave::saw, Wave::triangle })) == "A sine, a saw and a triangle");
}

TEST_CASE ("describe: wavetables take 'an' for A and 'a' for B and C", "[describe]")
{
    using dsp::Wave;
    REQUIRE (firstSentence (waves ({ Wave::wavetableA })) == "An A wavetable wave");
    REQUIRE (firstSentence (waves ({ Wave::wavetableA, Wave::wavetableB })) == "An A wavetable and a B wavetable");
    REQUIRE (firstSentence (waves ({ Wave::wavetableC, Wave::wavetableC })) == "Two C wavetables");
    REQUIRE (firstSentence (waves ({ Wave::saw, Wave::wavetableA, Wave::wavetableC })) == "A saw, an A wavetable and a C wavetable");
}

TEST_CASE ("describe: how far apart the oscillators are", "[describe]")
{
    using dsp::Wave;
    REQUIRE (firstSentence (twoSaws()) == "Two saws, the second 7 cents sharp");

    auto flat = twoSaws();
    flat.osc[1].fineCents = -1.0f;
    REQUIRE (firstSentence (flat) == "Two saws, the second one cent flat");

    auto octave = twoSaws();
    octave.osc[1].fineCents = 0.0f;
    octave.osc[1].octave = -1;
    REQUIRE (firstSentence (octave) == "Two saws, the second an octave down");

    octave.osc[1].octave = 0;
    octave.osc[1].semitone = 7;
    REQUIRE (firstSentence (octave) == "Two saws, the second 7 semitones up");
    octave.osc[1].semitone = 1;
    octave.osc[1].octave = 1;
    REQUIRE (firstSentence (octave) == "Two saws, the second an octave and a semitone up");

    auto three = waves ({ Wave::saw, Wave::saw, Wave::saw });
    three.osc[1].fineCents = 7.0f;
    REQUIRE (firstSentence (three) == "Three saws, the second 7 cents sharp");
    three.osc[2].fineCents = 7.0f;
    REQUIRE (firstSentence (three) == "Three saws, the other two 7 cents sharp");
    three.osc[2].fineCents = -12.0f;
    REQUIRE (firstSentence (three) == "Three saws tuned apart");

    auto mixed = waves ({ Wave::saw, Wave::sine });
    mixed.osc[1].octave = -2;
    REQUIRE (firstSentence (mixed) == "A saw and a sine two octaves down");

    auto trio = waves ({ Wave::saw, Wave::triangle, Wave::pulse });
    trio.osc[2].octave = 1;
    REQUIRE (firstSentence (trio) == "A saw, a triangle and a pulse an octave up");

    auto pair = waves ({ Wave::saw, Wave::saw, Wave::sine });
    pair.osc[1].fineCents = 7.0f;
    pair.osc[2].octave = -2;
    REQUIRE (firstSentence (pair) == "Two saws, the second 7 cents sharp, and a sine two octaves down");

    auto muted = twoSaws();
    muted.osc[1].levelDb = engine::minLevelDb; // bypassed or soloed away
    REQUIRE (firstSentence (muted) == "A saw wave");
}

TEST_CASE ("describe: the reference patch from the design", "[describe]")
{
    using dsp::Wave;
    auto p = waves ({ Wave::saw, Wave::saw, Wave::sine });
    p.osc[1].fineCents = 7.0f;
    p.osc[2].octave = -2;
    p.filter.enabled = true;
    p.filter.mode = dsp::FilterMode::lp24;
    p.filter.cutoffHz = 2000.0f;
    p.filter.shaperEnabled = true;
    p.filter.shaperMode = dsp::ShapeMode::hardClip;
    p.filterEnv.enabled = true;
    p.filterEnv.amount = 0.5f;
    p.ampEnv.enabled = true;
    p.lfo[0].enabled = true;
    p.mod[0] = { ModSource::lfo1, ModDest::pitchAll, 0.2f };

    REQUIRE (text (p) == "Two saws, the second 7 cents sharp, and a sine two octaves down, through a hard-clipped 24 dB low-pass at 2 kHz."
                         " A filter envelope opens it and a snappy amp envelope shapes each note."
                         " LFO 1 wobbles the pitch gently.");

    // The same patch with the design's modulation: LFO 1 on the cutoff and on LFO 2's speed, LFO 2 on the resonance.
    p.lfo[1].enabled = true;
    p.mod[0] = { ModSource::lfo1, ModDest::cutoff, 0.5f };
    p.mod[1] = { ModSource::lfo1, ModDest::lfo2Rate, 0.5f };
    p.mod[2] = { ModSource::lfo2, ModDest::resonance, 0.5f };
    p.chorus.enabled = true;
    REQUIRE (text (p).find (" LFO 1 wobbles the cutoff and speeds up LFO 2. LFO 2 moves the resonance. Then a chorus.") != std::string::npos);
}

TEST_CASE ("describe: filters, shapers and envelopes read cleanly", "[describe]")
{
    auto p = oneSaw();
    p.filter.enabled = true;
    p.filter.mode = dsp::FilterMode::notch;
    p.filter.cutoffHz = 1500.0f;
    p.filter.shaperEnabled = true;
    p.filter.shaperMode = dsp::ShapeMode::wavefolder;
    REQUIRE (firstSentence (p) == "A saw wave, through a wavefolded notch filter at 1.5 kHz");

    p.filter.shaperEnabled = false;
    p.filter.mode = dsp::FilterMode::comb;
    p.filter.cutoffHz = 440.0f;
    REQUIRE (firstSentence (p) == "A saw wave, through a comb filter at 440 Hz");

    p.filter.mode = dsp::FilterMode::lp12;
    p.filter.resonance = 0.7f;
    REQUIRE (firstSentence (p) == "A saw wave, through a 12 dB low-pass at 440 Hz with strong resonance");

    p.filterEnv.enabled = true;
    p.filterEnv.attack = 2.0f;
    p.filterEnv.amount = -0.5f;
    p.ampEnv.enabled = true;
    p.ampEnv.attack = 0.5f;
    REQUIRE (text (p).find ("A slow filter envelope closes it and a slow-building amp envelope shapes each note.") != std::string::npos);

    p.filterEnv.attack = 0.005f;
    p.filterEnv.decay = 0.005f;
    p.filterEnv.amount = 0.5f;
    p.ampEnv.attack = 0.1f;
    REQUIRE (text (p).find ("A fast filter envelope opens it and a smooth amp envelope shapes each note.") != std::string::npos);
}

TEST_CASE ("describe: noise, resonance and a snappy envelope", "[describe]")
{
    auto p = oneSaw();
    p.noise.enabled = true;
    p.noise.pink = true;
    p.noise.levelDb = -18.0f;
    p.filter.enabled = true;
    p.filter.mode = dsp::FilterMode::hp12;
    p.filter.cutoffHz = 440.0f;
    p.filter.resonance = 0.95f;
    p.ampEnv.enabled = true;
    p.ampEnv.attack = 0.005f;
    p.ampEnv.release = 0.1f;

    REQUIRE (text (p) == "A saw wave, with a little pink noise, through a 12 dB high-pass at 440 Hz,"
                         " ringing on the edge of self-oscillation. A snappy amp envelope shapes each note.");

    p.noise.levelDb = -6.0f;
    p.noise.pink = false;
    REQUIRE (firstSentence (p).find ("with plenty of white noise, through") != std::string::npos);
}

TEST_CASE ("describe: LFOs read as sentences of their own, in order", "[describe]")
{
    auto p = oneSaw();
    p.lfo[0].enabled = true;
    REQUIRE (text (p).find (" LFO 1 is running but connected to nothing.") != std::string::npos);

    p.filter.enabled = true;
    p.mod[0] = { ModSource::lfo1, ModDest::cutoff, 0.5f };
    p.mod[1] = { ModSource::lfo1, ModDest::pan, 0.5f };
    REQUIRE (text (p).find (" LFO 1 wobbles the cutoff and swirls the stereo position.") != std::string::npos);

    p.mod[2] = { ModSource::lfo1, ModDest::pitchAll, 0.9f };
    p.mod[3] = { ModSource::lfo1, ModDest::level1, 0.5f };
    REQUIRE (text (p).find (" LFO 1 wobbles the pitch deeply, wobbles the cutoff, adds a tremolo and swirls the stereo position.")
             != std::string::npos);

    p.mod[4] = { ModSource::velocity, ModDest::cutoff, 0.5f };
    REQUIRE (text (p).find (" Harder keys open the filter.") != std::string::npos);
}

TEST_CASE ("describe: the same patch always reads the same", "[describe]")
{
    auto p = twoSaws();
    p.lfo[1].enabled = true;
    p.mod[3] = { ModSource::lfo2, ModDest::pulseWidth1, -0.4f };
    REQUIRE (describe (p) == describe (p));
}

TEST_CASE ("describe: unison says how many copies", "[describe][m5]")
{
    auto p = oneSaw();
    p.osc[0].unison = 3;
    REQUIRE (text (p) == "A saw wave, played as three detuned copies. The keys switch it straight on and off.");

    p.osc[0].unisonDetuneCents = 0.0f;
    REQUIRE (text (p).find ("played as three identical copies") != std::string::npos);

    auto two = twoSaws();
    two.osc[0].unison = 5;
    two.osc[1].unison = 5;
    REQUIRE (text (two).find ("each played as five detuned copies") != std::string::npos);

    two.osc[1].unison = 2;
    REQUIRE (text (two).find ("some of it played as several detuned copies") != std::string::npos);

    p.osc[0].unison = 1;
    REQUIRE (text (p).find ("copies") == std::string::npos);
}

TEST_CASE ("describe: hard sync, ring modulation, FM and anti-aliasing off", "[describe][m5]")
{
    auto synced = twoSaws();
    synced.osc[1].sync = true;
    REQUIRE (firstSentence (synced) == "Two saws, the second 7 cents sharp, with Osc 2 hard-synced to Osc 1");

    synced.osc[2].enabled = true;
    synced.osc[2].sync = true;
    REQUIRE (text (synced).find ("with Osc 2 and Osc 3 hard-synced to Osc 1") != std::string::npos);

    // Sync needs the master: without oscillator 1 there is nothing to sync to.
    auto orphan = twoSaws();
    orphan.osc[0].enabled = false;
    orphan.osc[2].enabled = true;
    orphan.osc[2].sync = true;
    REQUIRE (text (orphan).find ("hard-synced") == std::string::npos);

    auto ring = twoSaws();
    ring.ringLevelDb = -12.0f;
    REQUIRE (text (ring).find (", with ring modulation between Osc 1 and Osc 2") != std::string::npos);
    ring.ringLevelDb = engine::minLevelDb; // off
    REQUIRE (text (ring).find ("ring") == std::string::npos);

    auto fm = twoSaws();
    fm.fmAmount = 0.5f;
    REQUIRE (text (fm).find (", with FM from Osc 1 into Osc 2") != std::string::npos);
    fm.fmAmount = 0.1f;
    REQUIRE (text (fm).find ("with a touch of FM from Osc 1 into Osc 2") != std::string::npos);

    fm.osc[2].enabled = true;
    fm.fmTarget = engine::FmTarget::both;
    fm.fmAmount = 0.9f;
    REQUIRE (text (fm).find ("with heavy FM from Osc 1 into Osc 2 and Osc 3") != std::string::npos);

    fm.fmTarget = engine::FmTarget::osc3;
    REQUIRE (text (fm).find ("FM from Osc 1 into Osc 3") != std::string::npos);

    fm.osc[2].enabled = false; // the target is absent: FM does nothing
    REQUIRE (text (fm).find ("FM") == std::string::npos);

    auto alias = oneSaw();
    alias.osc[0].antiAlias = false;
    REQUIRE (text (alias) == "A saw wave, with anti-aliasing switched off. The keys switch it straight on and off.");

    // Several extras share one "with", joined A, B and C.
    auto many = twoSaws();
    many.noise.enabled = true;
    many.osc[1].sync = true;
    many.osc[0].antiAlias = false;
    REQUIRE (firstSentence (many)
             == "Two saws, the second 7 cents sharp, with a little white noise, Osc 2 hard-synced to Osc 1 and anti-aliasing switched off");
}

TEST_CASE ("describe: effects read in chain order, and only the audible ones", "[describe][m5]")
{
    auto p = oneSaw();
    p.ampEnv.enabled = true;
    p.distortion.enabled = true;
    p.delay.enabled = true;
    p.delay.pingPong = true;
    p.delay.timeMs = 350.0f;
    p.reverb.enabled = true;

    REQUIRE (text (p) == "A saw wave. A snappy amp envelope shapes each note. Then distortion, a ping-pong delay of 350 ms and a reverb.");

    // Reordering changes the sentence.
    p.effectOrder = { engine::EffectKind::reverb, engine::EffectKind::delay, engine::EffectKind::chorus, engine::EffectKind::distortion };
    REQUIRE (text (p) == "A saw wave. A snappy amp envelope shapes each note. Then a reverb, a ping-pong delay of 350 ms and distortion.");

    // A dry effect is not audible; an absent one (not enabled, or bypassed away by the processor) is not mentioned.
    p.delay.mix = 0.0f;
    p.distortion.enabled = false;
    REQUIRE (text (p) == "A saw wave. A snappy amp envelope shapes each note. Then a reverb.");

    p.chorus.enabled = true;
    p.reverb.decaySeconds = 6.0f;
    REQUIRE (text (p) == "A saw wave. A snappy amp envelope shapes each note. Then a long reverb and a chorus.");

    p.reverb.enabled = false;
    p.chorus.enabled = false;
    REQUIRE (text (p) == "A saw wave. A snappy amp envelope shapes each note.");
}

TEST_CASE ("describe: distortion drive, and the limiter never appears", "[describe][m5]")
{
    auto p = oneSaw();
    p.distortion.enabled = true;

    p.distortion.driveDb = 4.0f;
    REQUIRE (text (p).find ("Then a touch of distortion.") != std::string::npos);

    p.distortion.driveDb = 30.0f;
    REQUIRE (text (p).find ("Then heavy distortion.") != std::string::npos);

    REQUIRE (text (oneSaw()).find ("limit") == std::string::npos);
}
