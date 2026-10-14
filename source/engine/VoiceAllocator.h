#pragma once

#include "engine/Voice.h"

#include <array>
#include <cstdint>

namespace scratchpad::engine
{

/** Owns every voice (pre-allocated at the maximum count) and decides which
    one plays each note. No allocation, locking or logging on any call.

    Stealing order (from the brief): released voices first, oldest release
    first; then held voices, oldest note first. A stolen voice fades over 5 ms
    before the new note starts on it. */
class VoiceAllocator
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate) noexcept;
    void setActiveVoices (int count) noexcept;

    void noteOn (int note, float velocity, bool preview, dsp::Wave wave, double bendSemitones) noexcept;
    void noteOff (int note, bool preview) noexcept;
    void setSustain (bool down) noexcept;

    /** Releases every voice. Preview voices are included only if asked. */
    void releaseAll (bool includePreview) noexcept;

    /** Adds all voices into `out` for `numSamples` samples. */
    void render (float* out, int numSamples, dsp::Wave currentWave, double bendSemitones) noexcept;

    int countActive() const noexcept;
    int getActiveVoiceLimit() const noexcept { return activeVoices; }

    std::array<Voice, maxVoices>& getVoices() noexcept { return voices; }
    const std::array<Voice, maxVoices>& getVoices() const noexcept { return voices; }

private:
    Voice* findFree() noexcept;
    Voice* findVictim() noexcept;

    std::array<Voice, maxVoices> voices;
    int activeVoices = 8;
    bool sustainDown = false;
    std::uint64_t counter = 0;
};

} // namespace scratchpad::engine
