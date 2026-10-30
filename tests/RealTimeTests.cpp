// The brief's real-time rule: nothing on the audio thread allocates. This counts heap allocations made by the
// thread that arms the counter, so a test can drive `processBlock` and require the count to stay at zero.
//
// With glibc `malloc`, `calloc` and `realloc` themselves are intercepted (the JUCE containers the engine meets,
// MidiMessage's SysEx storage among them, use malloc rather than `new`; `operator new` reaches malloc too).
// Elsewhere only `operator new` is counted.

#include "../tools/BenchmarkRig.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
    thread_local bool counting = false;
    std::atomic<long> allocations { 0 };

    void noteAllocation() noexcept
    {
        if (counting)
            allocations.fetch_add (1, std::memory_order_relaxed);
    }

    /** Counts the heap allocations this thread makes between `arm` and `disarm`. */
    class AllocationCounter
    {
    public:
        AllocationCounter() noexcept { allocations.store (0); }
        ~AllocationCounter() { disarm(); }

        void arm() noexcept { counting = true; }
        void disarm() noexcept { counting = false; }
        long count() const noexcept { return allocations.load(); }
    };
}

#if defined(__GLIBC__) && ! defined(__SANITIZE_ADDRESS__)

extern "C"
{
    void* __libc_malloc (std::size_t);
    void* __libc_calloc (std::size_t, std::size_t);
    void* __libc_realloc (void*, std::size_t);

    void* malloc (std::size_t size)                 { noteAllocation(); return __libc_malloc (size); }
    void* calloc (std::size_t count, std::size_t n) { noteAllocation(); return __libc_calloc (count, n); }
    void* realloc (void* p, std::size_t size)       { noteAllocation(); return __libc_realloc (p, size); }
}

#else

void* operator new (std::size_t size)
{
    noteAllocation();

    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)             { return operator new (size); }
void operator delete (void* p) noexcept             { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept           { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

#endif

using namespace scratchpad;

TEST_CASE ("The counter sees allocations, so a zero below means something", "[realtime]")
{
    AllocationCounter counter;
    const std::array<juce::uint8, 20> data {};

    counter.arm();
    juce::MidiMessage sysex (juce::MidiMessage::createSysExMessage (data.data(), 20)); // more than 8 bytes: on the heap
    counter.disarm();

    REQUIRE (sysex.getRawDataSize() > 8);
    REQUIRE (counter.count() >= 1);
}

TEST_CASE ("processBlock does not allocate: notes, controllers, SysEx, UI events and odd block sizes", "[realtime]")
{
    // Every module present, the extras on, eight voices: the heaviest patch the benchmark builds.
    bench::Rig rig ({ 7, true, true, true });
    auto& processor = rig.processor;

    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;

    AllocationCounter counter;

    const auto block = [&] (int numSamples, int note)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 100), numSamples / 4);
        midi.addEvent (juce::MidiMessage::pitchWheel (1, 12000), numSamples / 3);
        midi.addEvent (juce::MidiMessage::channelPressureChange (1, 90), numSamples / 2);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), numSamples / 2);
        midi.addEvent (juce::MidiMessage::createSysExMessage (std::array<juce::uint8, 40> {}.data(), 40), numSamples / 2);
        midi.addEvent (juce::MidiMessage::noteOff (1, note - 12), numSamples - 1);
        midi.addEvent (juce::MidiMessage::allNotesOff (1), numSamples - 1);

        buffer.setSize (2, numSamples, false, false, true); // shrinking in place does not reallocate

        counter.arm();
        processor.processBlock (buffer, midi);
        counter.disarm();
    };

    // Make everything the first blocks touch (lazy statics, first-use paths) happen before counting.
    for (int note : { 40, 52, 64 })
        block (256, note);

    processor.getUiEvents().push ({ plugin::UiEvent::Type::noteOn, 60, 0.8f });
    processor.getUiEvents().push ({ plugin::UiEvent::Type::pitchBend, 0, 0.3f });
    processor.getUiEvents().push ({ plugin::UiEvent::Type::modWheel, 0, 0.5f });

    for (int numSamples : { 1, 7, 32, 33, 64, 256, 480, 1024, 2048, 256 })
        for (int note : { 36, 48, 60, 72, 84 })
            block (numSamples, note);

    processor.getUiEvents().push ({ plugin::UiEvent::Type::allNotesOff, 0, 0.0f });
    block (256, 60);

    const long seen = counter.count();
    INFO (seen << " heap allocations on the audio thread");
    REQUIRE (seen == 0);
}
