#include "engine/SynthEngine.h"

#include <algorithm>

namespace scratchpad::engine
{

namespace
{
    constexpr double smoothingSeconds = 0.020;
}

void SynthEngine::prepare (double sampleRate)
{
    dsp::BlepTable::get(); // build the band-limiting table here, never on the audio thread
    allocator.prepare (sampleRate);
    allocator.setVoicing (params.voicing);

    for (auto& s : oscGain)
        s.reset (sampleRate, smoothingSeconds);

    for (auto* s : { &noiseGain, &previewGain, &cutoffLog2, &resonance, &driveDb, &filterEnvAmount, &outputGain })
        s->reset (sampleRate, smoothingSeconds);

    updateTargets (true);

    pitchBend = 0.0f;
    modWheel = 0.0f;
    aftertouch = 0.0f;
    prepared = true;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    // Losing oscillator 1 (New patch) lets held notes fade out rather than
    // hang around with nothing to play them.
    if (params.osc[0].enabled && ! p.osc[0].enabled)
        allocator.releaseAll (false);

    params = p;
    allocator.setVoicing (p.voicing);

    if (prepared)
        updateTargets (false);
}

void SynthEngine::updateTargets (bool jump) noexcept
{
    const auto target = BlockLevels::steady (params);

    auto set = [jump] (Smoother& s, float value)
    {
        if (jump)
            s.setCurrentAndTargetValue (value);
        else
            s.setTargetValue (value);
    };

    for (size_t i = 0; i < oscGain.size(); ++i)
        set (oscGain[i], target.osc[i].end);

    set (noiseGain, target.noise.end);
    set (previewGain, target.preview.end);
    set (cutoffLog2, target.cutoffLog2.end);
    set (resonance, target.resonance.end);
    set (driveDb, target.driveDb.end);
    set (filterEnvAmount, target.filterEnvAmount.end);
    set (outputGain, dbToGain (params.outputVolumeDb));
}

BlockLevels SynthEngine::advanceLevels (int numSamples) noexcept
{
    auto advance = [numSamples] (Smoother& s)
    {
        const float from = s.getCurrentValue();
        const float to = s.skip (numSamples);
        return Ramp { from, to };
    };

    BlockLevels levels;

    for (size_t i = 0; i < oscGain.size(); ++i)
        levels.osc[i] = advance (oscGain[i]);

    levels.noise = advance (noiseGain);
    levels.preview = advance (previewGain);
    levels.cutoffLog2 = advance (cutoffLog2);
    levels.resonance = advance (resonance);
    levels.driveDb = advance (driveDb);
    levels.filterEnvAmount = advance (filterEnvAmount);
    return levels;
}

void SynthEngine::noteOn (int note, float velocity) noexcept
{
    if (! params.osc[0].enabled)
        return;

    allocator.noteOn (note, velocity);
}

void SynthEngine::previewNoteOn (int note, dsp::Wave wave) noexcept
{
    allocator.noteOn (note, 0.8f, true, wave);
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
        const BlockLevels levels = advanceLevels (n);

        std::fill_n (mono.begin(), n, 0.0f);
        allocator.render (mono.data(), n, params, levels, bendSemitones());

        for (int i = 0; i < n; ++i)
        {
            const float s = mono[(size_t) i] * outputGain.getNextValue();
            left[done + i] = s;
            right[done + i] = s;
        }

        done += n;
    }
}

} // namespace scratchpad::engine
