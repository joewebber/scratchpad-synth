#pragma once

#include "engine/Voice.h"

#include <array>
#include <cstdint>

namespace scratchpad::engine
{

/** Owns every voice (pre-allocated at the maximum count) and decides which
    one plays each note. No allocation, locking or logging on any call.

    Poly: each note takes a free voice. When none is free, released voices are
    stolen first (oldest release first), then held voices (oldest note first);
    a stolen voice fades over 5 ms before the new note starts on it.

    Mono and legato: one voice and a last-note-priority stack of the keys that
    are down. Mono retriggers the envelopes on every note; legato only when no
    other key is held. Releasing the sounding note returns to the most recent
    key still down (retriggering in mono, gliding only in legato). The sustain
    pedal keeps released keys in the stack until it comes up. */
class VoiceAllocator
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate) noexcept;

    /** Mode, polyphony and glide. Changing the mode releases every played
        note (with the normal fades) so nothing hangs. */
    void setVoicing (const VoicingParams& voicing) noexcept;

    /** How many voices poly mode may use. Voices above it finish their note. */
    void setActiveVoices (int count) noexcept;

    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustain (bool down) noexcept;

    /** Releases every voice. */
    void releaseAll() noexcept;

    /** Adds all voices into `left` and `right`; `levels` cover exactly this block (at most `Voice::blockSize` samples). */
    void render (float* left, float* right, int numSamples, const EngineParams& params, const BlockLevels& levels, double bendSemitones, const Voice::Taps& taps) noexcept;

    void render (float* left, float* right, int numSamples, const EngineParams& params, const BlockLevels& levels, double bendSemitones) noexcept
    {
        render (left, right, numSamples, params, levels, bendSemitones, Voice::Taps {});
    }

    /** The sounding played voice that started most recently, or null. The editor's live modulation dots follow it. */
    const Voice* newestPlayedVoice() const noexcept;

    int countActive() const noexcept;
    int getActiveVoiceLimit() const noexcept { return activeVoices; }
    VoiceMode getMode() const noexcept { return mode; }

    std::array<Voice, maxVoices>& getVoices() noexcept { return voices; }
    const std::array<Voice, maxVoices>& getVoices() const noexcept { return voices; }

private:
    struct HeldKey
    {
        int note = 0;
        float velocity = 0.0f;
        bool sustained = false; // key is up, the pedal is keeping it
    };

    static constexpr int maxHeldKeys = 128;

    Voice* findFree() noexcept;
    Voice* findVictim() noexcept;
    Voice* findMonoVoice() noexcept;
    void startPoly (const Voice::Pending& note) noexcept;

    void monoNoteOn (const Voice::Pending& note) noexcept;
    void monoNoteOff (int note) noexcept;
    void followStack() noexcept;
    int indexOfKey (int note) const noexcept;
    void eraseKey (int index) noexcept;
    bool anyKeyDown() const noexcept;

    std::array<Voice, maxVoices> voices;
    std::array<HeldKey, maxHeldKeys> keys;
    int numKeys = 0;
    int activeVoices = 8;
    VoiceMode mode = VoiceMode::poly;
    bool sustainDown = false;
    std::uint64_t counter = 0;
};

} // namespace scratchpad::engine
