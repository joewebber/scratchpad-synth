#pragma once

#include "TestUtils.h"
#include "engine/SynthEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <utility>
#include <cmath>
#include <vector>

/** Shared helpers for the engine-level tests: a rig that drives a SynthEngine
    the way a host does, plus signal measurements. */
namespace enginetest
{

using namespace scratchpad;
using engine::EngineParams;
using engine::SynthEngine;

inline constexpr double sampleRate = 48000.0;

/** Oscillator 1 as a full-level sine; everything else off. */
inline EngineParams sineParams()
{
    EngineParams p;
    p.osc[0].enabled = true;
    p.osc[0].wave = dsp::Wave::sine;
    p.osc[0].levelDb = 0.0f;
    p.outputVolumeDb = 0.0f;
    return p;
}

/** Oscillator 1 present (notes are ignored without it) but silent; the other
    sources are left for the test to switch on. */
inline EngineParams silentParams()
{
    EngineParams p;
    p.osc[0].enabled = true;
    p.osc[0].levelDb = engine::minLevelDb;
    p.outputVolumeDb = 0.0f;
    return p;
}

struct Rig
{
    explicit Rig (EngineParams p = sineParams())
    {
        engine.setParams (p);
        engine.prepare (sampleRate);
        engine.setParams (p);
    }

    /** Renders in awkward block sizes, as hosts do. Returns the left channel. */
    std::vector<float> run (int numSamples)
    {
        std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);
        int done = 0;
        int block = 37;
        while (done < numSamples)
        {
            const int n = std::min (block, numSamples - done);
            engine.render (left.data() + done, right.data() + done, n);
            done += n;
            block = block == 37 ? 101 : 37;
        }
        REQUIRE (left == right);
        return left;
    }

    /** The same, keeping both channels (`first` left, `second` right), for pan. */
    std::pair<std::vector<float>, std::vector<float>> runStereo (int numSamples)
    {
        std::vector<float> left ((size_t) numSamples), right ((size_t) numSamples);
        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (64, numSamples - done);
            engine.render (left.data() + done, right.data() + done, n);
            done += n;
        }
        return { std::move (left), std::move (right) };
    }

    void midi (const juce::MidiMessage& m) { engine.handleMidi (m); }
    void noteOn (int note) { engine.handleMidi (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127)); }
    void noteOn (int note, juce::uint8 velocity) { engine.handleMidi (juce::MidiMessage::noteOn (1, note, velocity)); }
    void noteOff (int note) { engine.handleMidi (juce::MidiMessage::noteOff (1, note)); }
    void pedal (bool down) { engine.handleMidi (juce::MidiMessage::controllerEvent (1, 64, down ? 127 : 0)); }
    void set (const EngineParams& p) { engine.setParams (p); }
    int activeVoices() { return engine.getAllocator().countActive(); }

    SynthEngine engine;
};

inline std::vector<float> slice (const std::vector<float>& x, size_t from, size_t count)
{
    return std::vector<float> (x.begin() + (std::ptrdiff_t) from, x.begin() + (std::ptrdiff_t) (from + count));
}

inline float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (float s : x)
        p = std::max (p, std::abs (s));
    return p;
}

inline float peakOf (const std::vector<float>& x, size_t from, size_t count)
{
    return peak (slice (x, from, count));
}

inline float maxStep (const std::vector<float>& x, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from + 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

inline double rms (const std::vector<float>& x, size_t from, size_t count)
{
    double sum = 0.0;
    for (size_t i = from; i < from + count; ++i)
        sum += (double) x[i] * (double) x[i];
    return std::sqrt (sum / (double) count);
}

/** A frequency measured from the positive-going zero crossings of a (mostly)
    sinusoidal signal, and the sample time at the middle of what was measured. */
struct Measurement
{
    double hz = 0.0;
    double centre = 0.0;
};

inline Measurement measure (const std::vector<float>& x, size_t from, size_t count)
{
    std::vector<double> crossings;
    for (size_t i = from + 1; i < from + count; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            crossings.push_back ((double) (i - 1) + (double) -x[i - 1] / (double) (x[i] - x[i - 1]));

    REQUIRE (crossings.size() >= 1);
    const double span = crossings.back() - crossings.front();
    return { (double) (crossings.size() - 1) * sampleRate / span, 0.5 * (crossings.back() + crossings.front()) };
}

inline double measureHz (const std::vector<float>& x)
{
    return measure (x, 0, x.size()).hz;
}

inline double semitonesOf (double hz)
{
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

/** Root of the spectral energy between `loHz` and `hiHz`, normalised by the
    window length so a steady tone reads the same at any length. `x.size()`
    must be a power of two. */
inline double bandLevel (const std::vector<float>& x, double loHz, double hiHz)
{
    const auto mag = testutil::hannSpectrum (x);
    const double binHz = sampleRate / (double) x.size();

    double sum = 0.0;
    for (size_t k = 0; k < mag.size(); ++k)
    {
        const double f = (double) k * binHz;
        if (f >= loHz && f <= hiHz)
            sum += mag[k] * mag[k];
    }

    return std::sqrt (sum) / (double) x.size();
}

} // namespace enginetest
