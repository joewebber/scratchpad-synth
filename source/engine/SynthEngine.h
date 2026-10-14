#pragma once

#include "engine/VoiceAllocator.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

namespace scratchpad::engine
{

/** Plain values the engine needs from the parameter tree. The processor reads
    its cached atomics once per block and hands this over, so the engine never
    touches JUCE parameter objects. */
struct EngineParams
{
    bool oscEnabled = false;
    dsp::Wave wave = dsp::Wave::saw;
    float oscLevelDb = -6.0f;
    float outputVolumeDb = 0.0f;
    float pitchBendRangeSemitones = 2.0f;
};

/** Below this a level control means "off" (shown as -inf in the UI). */
inline constexpr float minLevelDb = -60.0f;

inline float dbToGain (float db) noexcept
{
    return db <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (db);
}

/** The whole sound engine for M1: MIDI in, stereo audio out.

    Until the patch has an oscillator (`oscEnabled`), notes from MIDI or the
    keyboard are ignored; only wave-picker preview notes sound. */
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
    static constexpr int subBlockSize = 32;

    void noteOn (int note, float velocity) noexcept;
    double bendSemitones() const noexcept { return (double) pitchBend * (double) params.pitchBendRangeSemitones; }

    VoiceAllocator allocator;
    EngineParams params;

    juce::SmoothedValue<float> oscGain;
    juce::SmoothedValue<float> outputGain;

    std::array<float, subBlockSize> mono {};

    float pitchBend = 0.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    bool prepared = false;
};

} // namespace scratchpad::engine
