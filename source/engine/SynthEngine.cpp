#include "engine/SynthEngine.h"

#include <algorithm>

namespace scratchpad::engine
{

namespace
{
    constexpr double smoothingSeconds = 0.020;
    constexpr double wheelSmoothingSeconds = 0.010;
}

void SynthEngine::prepare (double sampleRate)
{
    dsp::BlepTable::get(); // build the band-limiting table here, never on the audio thread
    allocator.prepare (sampleRate);
    allocator.setVoicing (params.voicing);

    for (auto& s : oscGain)
        s.reset (sampleRate, smoothingSeconds);

    for (auto& s : fmGain)
        s.reset (sampleRate, smoothingSeconds);

    for (auto* s : { &noiseGain, &ringGain, &cutoffLog2, &resonance, &driveDb, &filterEnvAmount, &outputGain })
        s->reset (sampleRate, smoothingSeconds);

    // A wheel moves in 128 steps; smooth them so modulating with it never zippers.
    modWheelSmoother.reset (sampleRate, wheelSmoothingSeconds);
    aftertouchSmoother.reset (sampleRate, wheelSmoothingSeconds);

    for (size_t i = 0; i < sharedLfo.size(); ++i)
    {
        sharedLfo[i].setSampleRate (sampleRate);
        sharedLfo[i].seed ((std::uint32_t) (0x51ed270bu + i * 7919u));
        sharedLfo[i].reset (0.0);
    }

    lfoValue = {};

    effects.prepare (sampleRate);
    limiter.prepare (sampleRate);
    limiterReductionDb.store (0.0f, std::memory_order_relaxed);

    updateTargets (true);

    pitchBend = 0.0f;
    modWheel = 0.0f;
    aftertouch = 0.0f;
    modWheelSmoother.setCurrentAndTargetValue (0.0f);
    aftertouchSmoother.setCurrentAndTargetValue (0.0f);
    prepared = true;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    // Losing oscillator 1 lets held notes fade out rather than hang around
    // with nothing to play them.
    if (params.osc[0].enabled && ! p.osc[0].enabled)
        allocator.releaseAll();

    // A free-running LFO that has just been added starts its cycle from the top.
    for (size_t i = 0; i < sharedLfo.size(); ++i)
        if (! params.lfo[i].enabled && p.lfo[i].enabled)
            sharedLfo[i].reset (0.0);

    params = p;
    effects.setParams (p);
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
    set (ringGain, target.ring.end);
    set (fmGain[0], target.fm[0].end);
    set (fmGain[1], target.fm[1].end);
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
    levels.ring = advance (ringGain);
    levels.fm = { advance (fmGain[0]), advance (fmGain[1]) };
    levels.cutoffLog2 = advance (cutoffLog2);
    levels.resonance = advance (resonance);
    levels.driveDb = advance (driveDb);
    levels.filterEnvAmount = advance (filterEnvAmount);

    levels.modWheel = modWheelSmoother.skip (numSamples);
    levels.aftertouch = aftertouchSmoother.skip (numSamples);

    // The shared LFOs. Their rate can only follow sources every voice agrees on:
    // the LFOs themselves (as of the last block), the mod wheel and aftertouch.
    ModSourceValues global {};
    global[index (ModSource::lfo1)] = lfoValue[0];
    global[index (ModSource::lfo2)] = lfoValue[1];
    global[index (ModSource::modWheel)] = levels.modWheel;
    global[index (ModSource::aftertouch)] = levels.aftertouch;
    const auto sums = accumulate (params.mod, global);

    for (size_t i = 0; i < sharedLfo.size(); ++i)
    {
        const auto& lp = params.lfo[i];

        if (lp.enabled)
        {
            sharedLfo[i].setShape (lp.shape);
            sharedLfo[i].setRate (modulatedLfoRate (lp.rateHz, sums[index (lfoRateDest (i))]));
            lfoValue[i] = sharedLfo[i].advance (numSamples);
        }
        else
        {
            lfoValue[i] = 0.0f;
        }
    }

    levels.lfo = lfoValue;
    return levels;
}

void SynthEngine::noteOn (int note, float velocity) noexcept
{
    if (! params.osc[0].enabled)
        return;

    allocator.noteOn (note, velocity);
}

void SynthEngine::setPitchBend (float normalised) noexcept
{
    pitchBend = std::clamp (normalised, -1.0f, 1.0f);
}

void SynthEngine::setModWheel (float normalised) noexcept
{
    modWheel = std::clamp (normalised, 0.0f, 1.0f);
    modWheelSmoother.setTargetValue (modWheel);
}

void SynthEngine::allNotesOff() noexcept
{
    allocator.releaseAll();
}

void SynthEngine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        allocator.noteOff (m.getNoteNumber());
    }
    else if (m.isPitchWheel())
    {
        setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    }
    else if (m.isChannelPressure())
    {
        aftertouch = (float) m.getChannelPressureValue() / 127.0f;
        aftertouchSmoother.setTargetValue (aftertouch);
    }
    else if (m.isController())
    {
        switch (m.getControllerNumber())
        {
            case 1:   setModWheel ((float) m.getControllerValue() / 127.0f); break;
            case 64:  allocator.setSustain (m.getControllerValue() >= 64); break;
            case 120: // all sound off
            case 123: allocator.releaseAll(); break;
            default:  break;
        }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        allocator.releaseAll();
    }
}

void SynthEngine::render (float* left, float* right, int numSamples) noexcept
{
    int done = 0;

    while (done < numSamples)
    {
        const int n = std::min (subBlockSize, numSamples - done);
        const BlockLevels levels = advanceLevels (n);

        std::fill_n (monoLeft.begin(), n, 0.0f);
        std::fill_n (monoRight.begin(), n, 0.0f);

        const auto tap = (ScopeTap) scopeTap.load (std::memory_order_relaxed);
        Voice::Taps taps;

        if (tap == ScopeTap::oscillators)
        {
            std::fill_n (tapOsc.begin(), n, 0.0f);
            taps.afterOscillators = tapOsc.data();
        }
        else if (tap == ScopeTap::filter)
        {
            std::fill_n (tapFilter.begin(), n, 0.0f);
            taps.afterFilter = tapFilter.data();
        }

        allocator.render (monoLeft.data(), monoRight.data(), n, params, levels, bendSemitones(), taps);

        // The voices' mix, for the amp tap, before the effects and the volume touch it.
        if (tap == ScopeTap::amp)
        {
            for (int i = 0; i < n; ++i)
                tapAmp[(size_t) i] = 0.5f * (monoLeft[(size_t) i] + monoRight[(size_t) i]) * outputGain.getTargetValue();
        }

        // Output stage: effects, volume, then the safety limiter.
        effects.process (monoLeft.data(), monoRight.data(), n);

        float* outLeft = left + done;
        float* outRight = right + done;

        for (int i = 0; i < n; ++i)
        {
            const float gain = outputGain.getNextValue();
            outLeft[i] = monoLeft[(size_t) i] * gain;
            outRight[i] = monoRight[(size_t) i] * gain;
        }

        limiter.process (outLeft, outRight, n);

        if (tap == ScopeTap::effects)
        {
            for (int i = 0; i < n; ++i)
                tapEffects[(size_t) i] = 0.5f * (outLeft[i] + outRight[i]);
        }

        {
            const auto write = scopeFifo.write (n);
            const float* source = tap == ScopeTap::oscillators ? tapOsc.data()
                                : tap == ScopeTap::filter      ? tapFilter.data()
                                : tap == ScopeTap::amp         ? tapAmp.data()
                                                               : tapEffects.data();

            for (int i = 0; i < write.blockSize1 + write.blockSize2; ++i)
            {
                const int slot = i < write.blockSize1 ? write.startIndex1 + i : write.startIndex2 + i - write.blockSize1;
                scopeData[(size_t) slot] = source[i];
            }
        }

        done += n;
    }

    limiterReductionDb.store (limiter.getReductionDb(), std::memory_order_relaxed);

    publishLive();
}

int SynthEngine::readScope (float* dest, int maxSamples) noexcept
{
    const auto read = scopeFifo.read (std::min (maxSamples, scopeFifo.getNumReady()));

    for (int i = 0; i < read.blockSize1; ++i)
        dest[i] = scopeData[(size_t) (read.startIndex1 + i)];

    for (int i = 0; i < read.blockSize2; ++i)
        dest[read.blockSize1 + i] = scopeData[(size_t) (read.startIndex2 + i)];

    return read.blockSize1 + read.blockSize2;
}

void SynthEngine::publishLive() noexcept
{
    const auto* voice = allocator.newestPlayedVoice();

    ModSourceValues values {};

    if (voice != nullptr)
    {
        values = voice->getModSources();
    }
    else
    {
        values[index (ModSource::lfo1)] = lfoValue[0];
        values[index (ModSource::lfo2)] = lfoValue[1];
        values[index (ModSource::modWheel)] = modWheelSmoother.getCurrentValue();
        values[index (ModSource::aftertouch)] = aftertouchSmoother.getCurrentValue();
    }

    for (size_t i = 0; i < values.size(); ++i)
        live.source[i].store (values[i], std::memory_order_relaxed);

    live.envStage[0].store (voice != nullptr ? voice->getAmpEnvStage() : 0, std::memory_order_relaxed);
    live.envStage[1].store (voice != nullptr ? voice->getFilterEnvStage() : 0, std::memory_order_relaxed);

    for (size_t i = 0; i < sharedLfo.size(); ++i)
    {
        const double phase = voice != nullptr && params.lfo[i].retrigger ? voice->getLfoPhase (i) : sharedLfo[i].getPhase();
        live.lfoPhase[i].store ((float) phase, std::memory_order_relaxed);
    }
}

} // namespace scratchpad::engine
