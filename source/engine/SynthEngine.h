#pragma once

#include "engine/BlockLevels.h"
#include "engine/EngineParams.h"
#include "engine/VoiceAllocator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace scratchpad::engine
{

/** The whole sound engine: MIDI in, stereo audio out.

    Until the patch has oscillator 1 (`osc[0].enabled`), notes from MIDI or the
    keyboard are ignored; only wave-picker preview notes sound. Every voice
    renders mono; both output channels carry the same signal.

    Patch-wide levels (oscillator, noise and output gains, filter cutoff,
    resonance, drive, envelope amount) are smoothed here over 20 ms, so a knob
    never steps, and handed to the voices a 32-sample sub-block at a time. */
class SynthEngine
{
public:
    void prepare (double sampleRate);
    void setParams (const EngineParams& newParams) noexcept;

    void handleMidi (const juce::MidiMessage& message) noexcept;
    void previewNoteOn (int note, dsp::Wave wave) noexcept;
    void previewNoteOff (int note) noexcept;
    void setPitchBend (float normalised) noexcept; // -1 .. +1
    void allNotesOff() noexcept;

    /** Overwrites `left` and `right` with `numSamples` of output. */
    void render (float* left, float* right, int numSamples) noexcept;

    VoiceAllocator& getAllocator() noexcept { return allocator; }

    float getModWheel() const noexcept { return modWheel; }
    float getAftertouch() const noexcept { return aftertouch; }

private:
    using Smoother = juce::SmoothedValue<float>;

    static constexpr int subBlockSize = Voice::blockSize;

    void noteOn (int note, float velocity) noexcept;
    void updateTargets (bool jump) noexcept;
    BlockLevels advanceLevels (int numSamples) noexcept;
    double bendSemitones() const noexcept { return (double) pitchBend * (double) params.voicing.bendRange; }

    VoiceAllocator allocator;
    EngineParams params;

    std::array<Smoother, 3> oscGain;
    Smoother noiseGain, previewGain, cutoffLog2, resonance, driveDb, filterEnvAmount;
    Smoother outputGain;

    std::array<float, subBlockSize> mono {};

    float pitchBend = 0.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    bool prepared = false;
};

} // namespace scratchpad::engine
