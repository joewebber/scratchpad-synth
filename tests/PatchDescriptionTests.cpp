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
