#include "engine/VoiceAllocator.h"

#include <algorithm>

namespace scratchpad::engine
{

void VoiceAllocator::prepare (double sampleRate) noexcept
{
    for (auto& v : voices)
        v.prepare (sampleRate);

    sustainDown = false;
    counter = 0;
}

void VoiceAllocator::setActiveVoices (int count) noexcept
{
    activeVoices = std::clamp (count, 1, maxVoices);

    // Voices above the new limit finish their note and are not used again.
    for (int i = activeVoices; i < maxVoices; ++i)
        if (voices[(size_t) i].state == Voice::State::held)
            voices[(size_t) i].release (++counter);
}

Voice* VoiceAllocator::findFree() noexcept
{
    for (int i = 0; i < activeVoices; ++i)
        if (voices[(size_t) i].state == Voice::State::idle)
            return &voices[(size_t) i];

    return nullptr;
}

Voice* VoiceAllocator::findVictim() noexcept
{
    Voice* oldestReleased = nullptr;
    Voice* oldestHeld = nullptr;
    Voice* anyStealing = nullptr;

    for (int i = 0; i < activeVoices; ++i)
    {
        auto& v = voices[(size_t) i];

        switch (v.state)
        {
            case Voice::State::releasing:
                if (oldestReleased == nullptr || v.releaseOrder < oldestReleased->releaseOrder)
                    oldestReleased = &v;
                break;

            case Voice::State::held:
                if (oldestHeld == nullptr || v.startOrder < oldestHeld->startOrder)
                    oldestHeld = &v;
                break;

            case Voice::State::stealing:
                anyStealing = &v;
                break;

            case Voice::State::idle:
                break;
        }
    }

    if (oldestReleased != nullptr) return oldestReleased;
    if (oldestHeld != nullptr)     return oldestHeld;
    return anyStealing;
}

void VoiceAllocator::noteOn (int note, float velocity, bool preview, dsp::Wave wave, double bendSemitones) noexcept
{
    if (auto* v = findFree())
    {
        v->start (note, velocity, preview, wave, bendSemitones);
        v->startOrder = ++counter;
        return;
    }

    if (auto* v = findVictim())
    {
        Voice::Pending p;
        p.note = note;
        p.velocity = velocity;
        p.preview = preview;
        p.wave = wave;

        // A voice that is already fading for another note gets its pending
        // note replaced; the newest request wins.
        v->steal (p);
        v->startOrder = ++counter;
    }
}

void VoiceAllocator::noteOff (int note, bool preview) noexcept
{
    for (auto& v : voices)
    {
        if (v.state == Voice::State::held && v.note == note && v.preview == preview && ! v.sustainPending)
        {
            if (sustainDown && ! preview)
                v.sustainPending = true;
            else
                v.release (++counter);
        }
        else if (v.state == Voice::State::stealing && v.hasPending && v.pending.note == note && v.pending.preview == preview)
        {
            if (sustainDown && ! preview)
                v.pending.keyReleased = true;
            else
                v.hasPending = false; // the new note never starts; voice just finishes fading
        }
    }
}

void VoiceAllocator::setSustain (bool down) noexcept
{
    if (sustainDown == down)
        return;

    sustainDown = down;

    if (down)
        return;

    for (auto& v : voices)
    {
        if (v.state == Voice::State::held && v.sustainPending)
            v.release (++counter);
        else if (v.state == Voice::State::stealing && v.hasPending && v.pending.keyReleased)
            v.hasPending = false;
    }
}

void VoiceAllocator::releaseAll (bool includePreview) noexcept
{
    for (auto& v : voices)
    {
        if (v.preview && ! includePreview)
            continue;

        if (v.state == Voice::State::held)
            v.release (++counter);
        else if (v.state == Voice::State::stealing)
            v.hasPending = false;
    }

    if (includePreview)
        sustainDown = false;
}

void VoiceAllocator::render (float* out, int numSamples, dsp::Wave currentWave, double bendSemitones) noexcept
{
    for (auto& v : voices)
        v.render (out, numSamples, currentWave, bendSemitones);
}

int VoiceAllocator::countActive() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.isActive(); });
}

} // namespace scratchpad::engine
