#pragma once

#include "dsp/Gate.h"
#include "dsp/Oscillator.h"

#include <cmath>
#include <cstdint>

namespace scratchpad::engine
{

/** One note of the per-voice chain. In M1 that is: oscillator -> declick gate.
    Later milestones add the mixer, filter and amp envelope between the two. */
class Voice
{
public:
    enum class State
    {
        idle,      // free, produces nothing
        held,      // key is down (or the sustain pedal is holding it)
        releasing, // key is up, gate is fading out
        stealing   // gate is fading out so `pending` can take over the voice
    };

    struct Pending
    {
        int note = -1;
        float velocity = 0.0f;
        bool preview = false;
        dsp::Wave wave = dsp::Wave::sine;
        bool keyReleased = false; // note-off arrived while the fade was running
    };

    static constexpr double waveSwitchSeconds = 0.010;

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        gate.setSampleRate (sr);
        waveFadeLength = std::max (1, (int) (waveSwitchSeconds * sr));
        kill();
    }

    void kill() noexcept
    {
        state = State::idle;
        hasPending = false;
        sustainPending = false;
        gate.reset();
    }

    /** Starts `n` immediately. The voice must be idle. */
    void start (int n, float vel, bool isPreview, dsp::Wave w, double bendSemitones, bool keyAlreadyReleased = false) noexcept
    {
        note = n;
        velocity = vel;
        preview = isPreview;
        wave = w;
        waveFadeRemaining = 0;
        sustainPending = keyAlreadyReleased;
        hasPending = false;
        osc.reset (0.0); // phase reset on note, on by default in the brief
        increment = incrementFor (bendSemitones);
        osc.setIncrement (increment);
        gate.open();
        state = State::held;
    }

    /** Fades out then starts `p`; used when every voice is busy. */
    void steal (const Pending& p) noexcept
    {
        pending = p;
        hasPending = true;
        gate.close (dsp::Gate::stealSeconds);
        state = State::stealing;
    }

    void release (std::uint64_t order) noexcept
    {
        sustainPending = false;
        releaseOrder = order;
        state = State::releasing;
        gate.close();
    }

    /** Adds this voice's output to `out`. `currentWave` is the patch's wave;
        preview voices ignore it and keep the wave they were started with. */
    void render (float* out, int numSamples, dsp::Wave currentWave, double bendSemitones) noexcept
    {
        if (state == State::idle)
            return;

        if (! preview && currentWave != wave)
        {
            waveFrom = wave;
            wave = currentWave;
            waveFadeRemaining = waveFadeLength;
        }

        const double targetIncrement = incrementFor (bendSemitones);
        double incrementStep = (targetIncrement - increment) / (double) numSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            increment += incrementStep;
            osc.setIncrement (increment);

            float s;
            if (waveFadeRemaining > 0)
            {
                const float x = 1.0f - (float) waveFadeRemaining / (float) waveFadeLength;
                s = osc.process (waveFrom, wave, x);
                --waveFadeRemaining;
            }
            else
            {
                s = osc.process (wave);
            }

            out[i] += s * gate.next();

            if (gate.isClosedAndSilent() && (state == State::releasing || state == State::stealing))
            {
                if (hasPending)
                {
                    const Pending p = pending;
                    start (p.note, p.velocity, p.preview, p.wave, bendSemitones, p.keyReleased);
                    incrementStep = 0.0;
                }
                else
                {
                    state = State::idle;
                    return;
                }
            }
        }
    }

    bool isActive() const noexcept { return state != State::idle; }

    State state = State::idle;
    int note = -1;
    float velocity = 0.0f;
    bool preview = false;
    bool sustainPending = false; // key is up but the pedal is holding the note
    std::uint64_t startOrder = 0;
    std::uint64_t releaseOrder = 0;
    bool hasPending = false;
    Pending pending;

    dsp::Wave wave = dsp::Wave::sine;
    dsp::Gate gate;

private:
    double incrementFor (double bendSemitones) const noexcept
    {
        const double hz = 440.0 * std::pow (2.0, ((double) note - 69.0 + bendSemitones) / 12.0);
        return hz / sampleRate;
    }

    dsp::Oscillator osc;
    dsp::Wave waveFrom = dsp::Wave::sine;
    int waveFadeRemaining = 0;
    int waveFadeLength = 480;
    double increment = 0.0;
    double sampleRate = 48000.0;
};

} // namespace scratchpad::engine
