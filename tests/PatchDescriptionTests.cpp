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

    EngineParams twoSaws()
    {
        auto p = oneSaw();
        p.osc[1].enabled = true;
        p.osc[1].wave = dsp::Wave::saw;
        p.osc[1].fineCents = 7.0f;
        return p;
    }
}

TEST_CASE ("describe: a raw wave is switched on and off by the keys", "[describe]")
{
    REQUIRE (describe (oneSaw()).toStdString() == "One saw wave, switched straight on and off by the keys.");
    REQUIRE (describe (EngineParams {}).toStdString() == "Nothing is making sound yet.");
}

TEST_CASE ("describe: oscillators say what they are and how far apart", "[describe]")
{
    REQUIRE (describe (twoSaws()).toStdString().rfind ("Two saws detuned by 7 cents", 0) == 0);

    auto octave = twoSaws();
    octave.osc[1].fineCents = 0.0f;
    octave.osc[1].octave = -1;
    REQUIRE (describe (octave).toStdString().rfind ("Two saws, an octave down", 0) == 0);

    auto mixed = oneSaw();
    mixed.osc[1].enabled = true;
    mixed.osc[1].wave = dsp::Wave::triangle;
    mixed.osc[2].enabled = true;
    mixed.osc[2].wave = dsp::Wave::pulse;
    REQUIRE (describe (mixed).toStdString().rfind ("A saw, a triangle and a pulse", 0) == 0);

    mixed.osc[2].octave = 1;
    REQUIRE (describe (mixed).toStdString().rfind ("A saw, a triangle and a pulse tuned apart", 0) == 0);

    auto muted = twoSaws();
    muted.osc[1].levelDb = engine::minLevelDb; // bypassed or soloed away
    REQUIRE (describe (muted).toStdString().rfind ("One saw wave", 0) == 0);
}

TEST_CASE ("describe: the reference patch from the brief", "[describe]")
{
    auto p = twoSaws();
    p.filter.enabled = true;
    p.filter.mode = dsp::FilterMode::lp24;
    p.filter.cutoffHz = 2000.0f;
    p.filterEnv.enabled = true;
    p.filterEnv.attack = 2.0f;
    p.filterEnv.amount = 0.5f;
    p.ampEnv.enabled = true;
    p.lfo[0].enabled = true;
    p.mod[0] = { ModSource::lfo1, ModDest::pitchAll, 0.2f };

    REQUIRE (describe (p).toStdString()
             == "Two saws detuned by 7 cents through a 24 dB low-pass filter at 2.00 kHz, opened by a very slow envelope,"
                " with a snappy amp envelope, with a gentle vibrato.");
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

    REQUIRE (describe (p).toStdString()
             == "One saw wave, with a little pink noise through a 12 dB high-pass filter at 440 Hz,"
                " ringing on the edge of self-oscillation, with a snappy amp envelope.");
}

TEST_CASE ("describe: an LFO that is connected to nothing says so, and motion reads in order", "[describe]")
{
    auto p = oneSaw();
    p.lfo[0].enabled = true;
    REQUIRE (describe (p).toStdString().find ("LFO 1 running but connected to nothing") != std::string::npos);

    p.filter.enabled = true;
    p.mod[0] = { ModSource::lfo1, ModDest::cutoff, 0.5f };
    p.mod[1] = { ModSource::lfo1, ModDest::pan, 0.5f };
    const auto text = describe (p).toStdString();
    REQUIRE (text.find ("with a wobbling filter and a swirling stereo position.") != std::string::npos);
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
    REQUIRE (describe (p).toStdString() == "One saw wave, played as three detuned copies, switched straight on and off by the keys.");

    p.osc[0].unisonDetuneCents = 0.0f;
    REQUIRE (describe (p).toStdString().find ("played as three identical copies") != std::string::npos);

    auto two = twoSaws();
    two.osc[0].unison = 5;
    two.osc[1].unison = 5;
    REQUIRE (describe (two).toStdString().find ("each played as five detuned copies") != std::string::npos);

    two.osc[1].unison = 2;
    REQUIRE (describe (two).toStdString().find ("some of it played as several detuned copies") != std::string::npos);

    p.osc[0].unison = 1;
    REQUIRE (describe (p).toStdString().find ("copies") == std::string::npos);
}

TEST_CASE ("describe: hard sync, ring modulation, FM and anti-aliasing off", "[describe][m5]")
{
    auto synced = twoSaws();
    synced.osc[1].sync = true;
    REQUIRE (describe (synced).toStdString().find ("Two saws detuned by 7 cents, with Osc 2 hard-synced to Osc 1") == 0);

    synced.osc[2].enabled = true;
    synced.osc[2].sync = true;
    REQUIRE (describe (synced).toStdString().find ("with Osc 2 and Osc 3 hard-synced to Osc 1") != std::string::npos);

    // Sync needs the master: without oscillator 1 there is nothing to sync to.
    auto orphan = twoSaws();
    orphan.osc[0].enabled = false;
    orphan.osc[2].enabled = true;
    orphan.osc[2].sync = true;
    REQUIRE (describe (orphan).toStdString().find ("hard-synced") == std::string::npos);

    auto ring = twoSaws();
    ring.ringLevelDb = -12.0f;
    REQUIRE (describe (ring).toStdString().find (", with ring modulation between Osc 1 and Osc 2") != std::string::npos);
    ring.ringLevelDb = engine::minLevelDb; // off
    REQUIRE (describe (ring).toStdString().find ("ring") == std::string::npos);

    auto fm = twoSaws();
    fm.fmAmount = 0.5f;
    REQUIRE (describe (fm).toStdString().find (", with FM from Osc 1 into Osc 2") != std::string::npos);
    fm.fmAmount = 0.1f;
    REQUIRE (describe (fm).toStdString().find ("with a touch of FM from Osc 1 into Osc 2") != std::string::npos);

    fm.osc[2].enabled = true;
    fm.fmTarget = engine::FmTarget::both;
    fm.fmAmount = 0.9f;
    REQUIRE (describe (fm).toStdString().find ("with heavy FM from Osc 1 into Osc 2 and Osc 3") != std::string::npos);

    fm.fmTarget = engine::FmTarget::osc3;
    REQUIRE (describe (fm).toStdString().find ("FM from Osc 1 into Osc 3") != std::string::npos);

    fm.osc[2].enabled = false; // the target is absent: FM does nothing
    REQUIRE (describe (fm).toStdString().find ("FM") == std::string::npos);

    auto alias = oneSaw();
    alias.osc[0].antiAlias = false;
    REQUIRE (describe (alias).toStdString()
             == "One saw wave, with anti-aliasing switched off, switched straight on and off by the keys.");
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

    REQUIRE (describe (p).toStdString()
             == "One saw wave, shaped by a snappy amp envelope, then through distortion, a ping-pong delay of 350 ms and a reverb.");

    // Reordering changes the sentence.
    p.effectOrder = { engine::EffectKind::reverb, engine::EffectKind::delay, engine::EffectKind::chorus, engine::EffectKind::distortion };
    REQUIRE (describe (p).toStdString()
             == "One saw wave, shaped by a snappy amp envelope, then through a reverb, a ping-pong delay of 350 ms and distortion.");

    // A dry effect is not audible; an absent one (not enabled, or bypassed away by the processor) is not mentioned.
    p.delay.mix = 0.0f;
    p.distortion.enabled = false;
    REQUIRE (describe (p).toStdString() == "One saw wave, shaped by a snappy amp envelope, then through a reverb.");

    p.chorus.enabled = true;
    p.reverb.decaySeconds = 6.0f;
    REQUIRE (describe (p).toStdString()
             == "One saw wave, shaped by a snappy amp envelope, then through a long reverb and a chorus.");

    p.reverb.enabled = false;
    p.chorus.enabled = false;
    REQUIRE (describe (p).toStdString() == "One saw wave, shaped by a snappy amp envelope.");
}

TEST_CASE ("describe: distortion drive, and the limiter never appears", "[describe][m5]")
{
    auto p = oneSaw();
    p.distortion.enabled = true;

    p.distortion.driveDb = 4.0f;
    REQUIRE (describe (p).toStdString().find ("then through a touch of distortion.") != std::string::npos);

    p.distortion.driveDb = 30.0f;
    REQUIRE (describe (p).toStdString().find ("then through heavy distortion.") != std::string::npos);

    REQUIRE (describe (oneSaw()).toStdString().find ("limit") == std::string::npos);
}
