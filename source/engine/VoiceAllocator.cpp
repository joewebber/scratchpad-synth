#include "engine/VoiceAllocator.h"

#include <algorithm>

namespace scratchpad::engine
{

namespace
{
    /** Which kind of note the voice is working for: while it fades for a
        steal, that is the note waiting to start. */
    bool servesPreview (const Voice& v) noexcept
    {
        return (v.state == Voice::State::stealing && v.hasPending) ? v.pending.preview : v.preview;
    }
}

void VoiceAllocator::prepare (double sampleRate) noexcept
{
    for (size_t i = 0; i < voices.size(); ++i)
        voices[i].prepare (sampleRate, 0x9e3779b9u * (std::uint32_t) (i + 1)); // each voice its own noise

    sustainDown = false;
    numKeys = 0;
    counter = 0;
}

void VoiceAllocator::setVoicing (const VoicingParams& v) noexcept
{
    if (v.mode != mode)
    {
        releaseAll (false);
        mode = v.mode;
    }

    setActiveVoices (v.voices);

    for (auto& voice : voices)
        voice.setGlideSeconds ((double) v.glideSeconds);
}

void VoiceAllocator::setActiveVoices (int count) noexcept
{
    activeVoices = std::clamp (count, 1, maxVoices);

    if (mode != VoiceMode::poly)
        return;

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

/** The voice mono and legato play on: whichever one is already working on a
    played (not preview) note. */
Voice* VoiceAllocator::findMonoVoice() noexcept
{
    for (auto& v : voices)
        if (v.state != Voice::State::idle && ! servesPreview (v))
            return &v;

    return nullptr;
}

void VoiceAllocator::startPoly (const Voice::Pending& note) noexcept
{
    if (auto* v = findFree())
    {
        v->start (note);
        v->startOrder = ++counter;
        return;
    }

    // A voice that is already fading for another note gets its pending
    // note replaced; the newest request wins.
    if (auto* v = findVictim())
    {
        v->steal (note);
        v->startOrder = ++counter;
    }
}

void VoiceAllocator::noteOn (int note, float velocity, bool preview, dsp::Wave previewWave) noexcept
{
    Voice::Pending n;
    n.note = note;
    n.velocity = velocity;
    n.preview = preview;
    n.wave = previewWave;

    if (preview || mode == VoiceMode::poly)
        startPoly (n);
    else
        monoNoteOn (n);
}

void VoiceAllocator::noteOff (int note, bool preview) noexcept
{
    if (! preview && mode != VoiceMode::poly)
    {
        monoNoteOff (note);
        return;
    }

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

    // Mono and legato: keys the pedal was keeping leave the stack now, and if
    // one of them was sounding the voice moves on to the key below it.
    const bool soundingLeaves = numKeys > 0 && keys[(size_t) numKeys - 1].sustained;

    int kept = 0;
    for (int i = 0; i < numKeys; ++i)
        if (! keys[(size_t) i].sustained)
            keys[(size_t) kept++] = keys[(size_t) i];
    numKeys = kept;

    if (soundingLeaves)
        followStack();
}

void VoiceAllocator::releaseAll (bool includePreview) noexcept
{
    for (auto& v : voices)
    {
        if (v.state == Voice::State::held && (includePreview || ! v.preview))
            v.release (++counter);
        else if (v.state == Voice::State::stealing && v.hasPending && (includePreview || ! v.pending.preview))
            v.hasPending = false;
    }

    numKeys = 0;

    if (includePreview)
        sustainDown = false;
}

void VoiceAllocator::render (float* left, float* right, int numSamples, const EngineParams& params, const BlockLevels& levels, double bendSemitones, const Voice::Taps& taps) noexcept
{
    for (auto& v : voices)
        v.render (left, right, numSamples, params, levels, bendSemitones, taps);
}

const Voice* VoiceAllocator::newestPlayedVoice() const noexcept
{
    const Voice* newest = nullptr;

    for (const auto& v : voices)
        if (v.isActive() && ! v.preview && (newest == nullptr || v.startOrder > newest->startOrder))
            newest = &v;

    return newest;
}

int VoiceAllocator::countActive() const noexcept
{
    return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.isActive(); });
}

void VoiceAllocator::monoNoteOn (const Voice::Pending& note) noexcept
{
    // Legato glides instead of retriggering only if a key is physically down;
    // a note the pedal is merely holding counts as released.
    const bool keyWasDown = anyKeyDown();

    if (const int existing = indexOfKey (note.note); existing >= 0)
        eraseKey (existing);

    if (numKeys < maxHeldKeys)
        keys[(size_t) numKeys++] = { note.note, note.velocity, false };

    auto* v = findMonoVoice();

    if (v == nullptr)
    {
        startPoly (note);
    }
    else if (v->state == Voice::State::stealing)
    {
        v->steal (note);
    }
    else
    {
        const bool glideOnly = mode == VoiceMode::legato && v->state == Voice::State::held && keyWasDown;
        v->retrigger (note, ! glideOnly);
        v->startOrder = ++counter;
    }
}

void VoiceAllocator::monoNoteOff (int note) noexcept
{
    const int index = indexOfKey (note);

    if (index < 0)
        return;

    if (sustainDown)
    {
        keys[(size_t) index].sustained = true;
        return;
    }

    const bool wasSounding = index == numKeys - 1;
    eraseKey (index);

    if (wasSounding)
        followStack();
}

/** Points the mono voice at the top of the stack, or releases it if the stack is empty. */
void VoiceAllocator::followStack() noexcept
{
    auto* v = findMonoVoice();

    if (v == nullptr)
        return;

    if (numKeys == 0)
    {
        if (v->state == Voice::State::stealing)
            v->hasPending = false;
        else if (v->state == Voice::State::held)
            v->release (++counter);

        return;
    }

    Voice::Pending n;
    n.note = keys[(size_t) numKeys - 1].note;
    n.velocity = keys[(size_t) numKeys - 1].velocity;

    if (v->state == Voice::State::stealing)
        v->steal (n);
    else
        v->retrigger (n, mode == VoiceMode::mono); // legato slides back without a new attack
}

int VoiceAllocator::indexOfKey (int note) const noexcept
{
    for (int i = 0; i < numKeys; ++i)
        if (keys[(size_t) i].note == note)
            return i;

    return -1;
}

void VoiceAllocator::eraseKey (int index) noexcept
{
    for (int i = index; i < numKeys - 1; ++i)
        keys[(size_t) i] = keys[(size_t) i + 1];

    --numKeys;
}

bool VoiceAllocator::anyKeyDown() const noexcept
{
    for (int i = 0; i < numKeys; ++i)
        if (! keys[(size_t) i].sustained)
            return true;

    return false;
}

} // namespace scratchpad::engine
