#include "engine/SynthEngine.h"

#include <algorithm>

namespace scratchpad::engine
{

void SynthEngine::prepare (double sampleRate)
{
    dsp::BlepTable::get(); // build the band-limiting table here, never on the audio thread
    allocator.prepare (sampleRate);

    oscGain.reset (sampleRate, 0.020);   // gain smoothing over 20 ms
    outputGain.reset (sampleRate, 0.020);
    oscGain.setCurrentAndTargetValue (dbToGain (params.oscLevelDb));
    outputGain.setCurrentAndTargetValue (dbToGain (params.outputVolumeDb));

    pitchBend = 0.0f;
    modWheel = 0.0f;
    aftertouch = 0.0f;
    prepared = true;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    // Losing the oscillator (New patch) lets held notes fade out rather than
    // hang around with nothing to play them.
    if (params.oscEnabled && ! p.oscEnabled)
        allocator.releaseAll (false);

    params = p;

    if (prepared)
    {
        oscGain.setTargetValue (dbToGain (p.oscLevelDb));
        outputGain.setTargetValue (dbToGain (p.outputVolumeDb));
    }
}

void SynthEngine::noteOn (int note, float velocity) noexcept
{
    if (! params.oscEnabled)
        return;

    allocator.noteOn (note, velocity, false, params.wave, bendSemitones());
}

void SynthEngine::previewNoteOn (int note, dsp::Wave wave) noexcept
{
    allocator.noteOn (note, 0.8f, true, wave, 0.0);
}

void SynthEngine::previewNoteOff (int note) noexcept
{
    allocator.noteOff (note, true);
}

void SynthEngine::setPitchBend (float normalised) noexcept
{
    pitchBend = std::clamp (normalised, -1.0f, 1.0f);
}

void SynthEngine::allNotesOff() noexcept
{
    allocator.releaseAll (false);
}

void SynthEngine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        allocator.noteOff (m.getNoteNumber(), false);
    }
    else if (m.isPitchWheel())
    {
        setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    }
    else if (m.isChannelPressure())
    {
        aftertouch = (float) m.getChannelPressureValue() / 127.0f;
    }
    else if (m.isController())
    {
        switch (m.getControllerNumber())
        {
            case 1:   modWheel = (float) m.getControllerValue() / 127.0f; break;
            case 64:  allocator.setSustain (m.getControllerValue() >= 64); break;
            case 120: // all sound off
            case 123: allocator.releaseAll (false); break;
            default:  break;
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        allocator.releaseAll (false);
    }
}

void SynthEngine::render (float* left, float* right, int numSamples) noexcept
{
    int done = 0;

    while (done < numSamples)
    {
        const int n = std::min (subBlockSize, numSamples - done);

        std::fill_n (mono.begin(), n, 0.0f);
        allocator.render (mono.data(), n, params.wave, bendSemitones());

        for (int i = 0; i < n; ++i)
        {
            const float g = oscGain.getNextValue() * outputGain.getNextValue();
            const float s = mono[(size_t) i] * g;
            left[done + i] = s;
            right[done + i] = s;
        }

        done += n;
    }
}

} // namespace scratchpad::engine
