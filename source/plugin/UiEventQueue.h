#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace scratchpad::plugin
{

/** An event from the on-screen keyboard, wheels or wave picker to the audio
    thread. */
struct UiEvent
{
    enum class Type : std::uint8_t
    {
        noteOn,
        noteOff,
        previewOn,  // wave-picker preview note; `wave` is the wave index
        previewOff,
        pitchBend,  // `value` in -1..+1
        modWheel,   // `value` in 0..1
        allNotesOff
    };

    Type type = Type::noteOn;
    std::uint8_t note = 0;
    std::uint8_t wave = 0;
    float value = 0.0f; // velocity (0..1), bend (-1..+1) or mod wheel (0..1)
};

/** Single-producer (message thread), single-consumer (audio thread) queue.
    Wait-free on both sides; a full queue drops the new event rather than
    block. The editor pushes, `processBlock` drains. */
class UiEventQueue
{
public:
    bool push (const UiEvent& e) noexcept
    {
        const auto scope = fifo.write (1);

        if (scope.blockSize1 + scope.blockSize2 < 1)
            return false;

        slots[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = e;
        return true;
    }

    template <typename Fn>
    void drain (Fn&& fn) noexcept
    {
        const auto scope = fifo.read (fifo.getNumReady());

        for (int i = 0; i < scope.blockSize1; ++i)
            fn (slots[(size_t) (scope.startIndex1 + i)]);

        for (int i = 0; i < scope.blockSize2; ++i)
            fn (slots[(size_t) (scope.startIndex2 + i)]);
    }

private:
    static constexpr int capacity = 512;

    juce::AbstractFifo fifo { capacity };
    std::array<UiEvent, capacity> slots {};
};

} // namespace scratchpad::plugin
