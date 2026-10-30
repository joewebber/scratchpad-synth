#pragma once

#include "dsp/Limiter.h"
#include "engine/BlockLevels.h"
#include "engine/EffectsChain.h"
#include "engine/EngineParams.h"
#include "engine/VoiceAllocator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace scratchpad::engine
{

/** What the audio thread tells the editor about modulation, for the live dots
    and the LFO pictures. Written once per block with relaxed atomics, read by
    the message thread, never both on the same value in a way that matters.
    Everything follows the most recently started voice, or the shared values when
    nothing is sounding. */
/** Where the scope listens. The values are saved nowhere: it is a view, not part of the patch. */
enum class ScopeTap : int { oscillators = 0, filter, amp, effects };

inline constexpr int scopeFifoSize = 8192;

struct LiveModulation
{
    std::array<std::atomic<float>, numModSources> source {}; // indexed by ModSource, as `Voice::getModSources`
    std::array<std::atomic<float>, 2> lfoPhase {};           // 0 .. 1
    std::array<std::atomic<int>, 2> envStage {};             // amp, filter: `dsp::Envelope::Stage` of the newest note
};

/** The whole sound engine: MIDI in, stereo audio out.

    Until the patch has oscillator 1 (`osc[0].enabled`), notes from MIDI or the
    keyboard are ignored. Every voice renders stereo: the two channels differ
    only where a connection moves pan.

    Patch-wide levels (oscillator, noise and output gains, filter cutoff,
    resonance, drive, envelope amount) are smoothed here over 20 ms, so a knob
    never steps, and handed to the voices a 32-sample sub-block at a time. */
class SynthEngine
{
public:
    void prepare (double sampleRate);
    void setParams (const EngineParams& newParams) noexcept;

    void handleMidi (const juce::MidiMessage& message) noexcept;
    void setPitchBend (float normalised) noexcept; // -1 .. +1
    void setModWheel (float normalised) noexcept;  // 0 .. 1
    void allNotesOff() noexcept;

    /** Overwrites `left` and `right` with `numSamples` of output. */
    void render (float* left, float* right, int numSamples) noexcept;

    VoiceAllocator& getAllocator() noexcept { return allocator; }

    /** Which signal the scope receives. Any thread. */
    void setScopeTap (ScopeTap t) noexcept { scopeTap.store ((int) t, std::memory_order_relaxed); }

    /** Moves up to `maxSamples` queued scope samples into `dest`; returns how many. Message thread. */
    int readScope (float* dest, int maxSamples) noexcept;

    const LiveModulation& getLiveModulation() const noexcept { return live; }

    float getModWheel() const noexcept { return modWheel; }
    float getAftertouch() const noexcept { return aftertouch; }

    /** How far the safety limiter is pulling the output down, in dB (0 when it is idle). Smoothed; any thread. */
    float getLimiterReductionDb() const noexcept { return limiterReductionDb.load (std::memory_order_relaxed); }

    /** Delay the effects add (distortion's oversampling), in samples. Constant for a sample rate and reported
        even while distortion is off, so latency never jumps. Valid after `prepare`. */
    int getEffectsLatencySamples() const noexcept { return effects.getLatencySamples(); }

private:
    using Smoother = juce::SmoothedValue<float>;

    static constexpr int subBlockSize = Voice::blockSize;

    void noteOn (int note, float velocity) noexcept;
    void publishLive() noexcept;
    void updateTargets (bool jump) noexcept;
    BlockLevels advanceLevels (int numSamples) noexcept;
    double bendSemitones() const noexcept { return (double) pitchBend * (double) params.voicing.bendRange; }

    VoiceAllocator allocator;
    EngineParams params;

    std::array<Smoother, 3> oscGain;
    Smoother noiseGain, ringGain, cutoffLog2, resonance, driveDb, filterEnvAmount;
    std::array<Smoother, 2> fmGain; // FM amount into oscillators 2 and 3
    Smoother outputGain;
    Smoother modWheelSmoother, aftertouchSmoother;

    // The LFOs that are not set to retrigger: one phase, shared by every voice.
    std::array<dsp::Lfo, 2> sharedLfo;
    std::array<float, 2> lfoValue {};
    LiveModulation live;

    std::atomic<int> scopeTap { (int) ScopeTap::amp };
    juce::AbstractFifo scopeFifo { scopeFifoSize };
    std::array<float, scopeFifoSize> scopeData {};
    std::array<float, subBlockSize> tapOsc {}, tapFilter {};
    std::array<float, subBlockSize> tapAmp {}, tapEffects {};

    EffectsChain effects;
    dsp::Limiter limiter;
    std::atomic<float> limiterReductionDb { 0.0f };

    std::array<float, subBlockSize> monoLeft {}, monoRight {};

    float pitchBend = 0.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    bool prepared = false;
};

} // namespace scratchpad::engine
