#include "plugin/PluginProcessor.h"

#include "plugin/Parameters.h"
#include "plugin/PatchState.h"
#include "plugin/PluginEditor.h"

namespace scratchpad::plugin
{

namespace
{
    // Parameter changes made through the host or a control reach the
    // ValueTree on a timer. Writing into a stale tree would silently do
    // nothing (or undo the wrong thing), so catch it up first. This also
    // puts any pending drag into its own, earlier undo transaction.
    void flushPendingChanges (juce::AudioProcessorValueTreeState& apvts)
    {
        (void) apvts.copyState();
    }

    void setParamValue (juce::AudioProcessorValueTreeState& apvts, const char* id, float unnormalisedValue)
    {
        // Goes through the ValueTree with the undo manager, so the change is
        // recorded in the current transaction straight away.
        auto v = apvts.getParameterAsValue (id);
        v = unnormalisedValue;
    }

    float defaultOf (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* p = apvts.getParameter (id);
        return p->convertFrom0to1 (p->getDefaultValue());
    }
}

ScratchpadProcessor::ScratchpadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "ScratchpadSynth", params::createLayout())
{
    for (size_t i = 0; i < oscPointers.size(); ++i)
    {
        const auto& ids = params::osc[i];
        oscPointers[i] = { findParam (ids.enabled), findParam (ids.wave), findParam (ids.octave), findParam (ids.semitone),
                           findParam (ids.fine), findParam (ids.pulseWidth), findParam (ids.level), findParam (ids.phaseReset) };
    }

    ampPointers = { findParam (params::ampEnv.enabled), findParam (params::ampEnv.attack), findParam (params::ampEnv.decay),
                    findParam (params::ampEnv.sustain), findParam (params::ampEnv.release) };
    filterEnvPointers = { findParam (params::filterEnv.enabled), findParam (params::filterEnv.attack),
                          findParam (params::filterEnv.decay), findParam (params::filterEnv.sustain),
                          findParam (params::filterEnv.release) };

    noiseEnabled = findParam (params::noiseEnabled);
    noiseColour = findParam (params::noiseColour);
    noiseLevel = findParam (params::noiseLevel);
    filterEnabled = findParam (params::filterEnabled);
    filterMode = findParam (params::filterMode);
    filterCutoff = findParam (params::filterCutoff);
    filterResonance = findParam (params::filterResonance);
    filterKeyTrack = findParam (params::filterKeyTrack);
    filterDrive = findParam (params::filterDrive);
    ampVelocity = findParam (params::ampEnvVelocity);
    filterEnvAmount = findParam (params::filterEnvAmount);
    voicingMode = findParam (params::voicingMode);
    voicingVoices = findParam (params::voicingVoices);
    voicingGlide = findParam (params::voicingGlide);
    voicingBendRange = findParam (params::voicingBendRange);
    outVolume = findParam (params::outVolume);
}

std::atomic<float>* ScratchpadProcessor::findParam (const char* id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

void ScratchpadProcessor::prepareToPlay (double sampleRate, int)
{
    synthEngine.prepare (sampleRate);
    synthEngine.setParams (readParams());

    // The band-limited oscillators run a fixed number of samples late.
    setLatencySamples (dsp::Oscillator::latencySamples);
}

bool ScratchpadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
        && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

engine::EngineParams ScratchpadProcessor::readParams() const noexcept
{
    const auto load = [] (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); };
    const auto on = [&] (const std::atomic<float>* p) noexcept { return load (p) >= 0.5f; };
    const auto index = [&] (const std::atomic<float>* p, int count) noexcept
    {
        return juce::jlimit (0, count - 1, (int) (load (p) + 0.5f));
    };
    const auto fraction = [&] (const std::atomic<float>* p) noexcept { return load (p) * 0.01f; };

    engine::EngineParams e;

    for (size_t i = 0; i < oscPointers.size(); ++i)
    {
        const auto& s = oscPointers[i];
        auto& o = e.osc[i];
        o.enabled = on (s.enabled);
        o.wave = (dsp::Wave) index (s.wave, dsp::numWaves);
        o.octave = juce::jlimit (-2, 2, juce::roundToInt (load (s.octave)));
        o.semitone = juce::jlimit (-12, 12, juce::roundToInt (load (s.semitone)));
        o.fineCents = load (s.fine);
        o.pulseWidth = fraction (s.pulseWidth);
        o.levelDb = load (s.level);
        o.phaseReset = on (s.phaseReset);
    }

    e.noise.enabled = on (noiseEnabled);
    e.noise.pink = index (noiseColour, 2) == 1;
    e.noise.levelDb = load (noiseLevel);

    e.filter.enabled = on (filterEnabled);
    e.filter.mode = (dsp::FilterMode) index (filterMode, dsp::numFilterModes);
    e.filter.cutoffHz = load (filterCutoff);
    e.filter.resonance = fraction (filterResonance);
    e.filter.keyTracking = fraction (filterKeyTrack);
    e.filter.driveDb = load (filterDrive);

    const auto readEnvelope = [&] (const EnvPointers& s, engine::EnvelopeParams& env)
    {
        env.enabled = on (s.enabled);
        env.attack = load (s.attack);
        env.decay = load (s.decay);
        env.sustain = fraction (s.sustain);
        env.release = load (s.release);
    };

    readEnvelope (ampPointers, e.ampEnv);
    e.ampEnv.velocitySensitivity = fraction (ampVelocity);
    readEnvelope (filterEnvPointers, e.filterEnv);
    e.filterEnv.amount = fraction (filterEnvAmount);

    e.voicing.mode = (engine::VoiceMode) index (voicingMode, 3);
    e.voicing.voices = juce::jlimit (1, params::maxVoices, juce::roundToInt (load (voicingVoices)));
    e.voicing.glideSeconds = load (voicingGlide);
    e.voicing.bendRange = load (voicingBendRange);

    e.outputVolumeDb = load (outVolume);
    return e;
}

void ScratchpadProcessor::renderRange (juce::AudioBuffer<float>& buffer, int start, int count) noexcept
{
    if (count <= 0)
        return;

    auto* left = buffer.getWritePointer (0) + start;
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) + start : nullptr;

    if (right != nullptr)
    {
        synthEngine.render (left, right, count);
    }
    else
    {
        // Mono bus: the engine writes the same samples to both pointers, so
        // pointing both at the one channel is harmless.
        synthEngine.render (left, left, count);
    }
}

void ScratchpadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    synthEngine.setParams (readParams());

    uiEvents.drain ([this] (const UiEvent& e)
    {
        switch (e.type)
        {
            case UiEvent::Type::noteOn:
                synthEngine.handleMidi (juce::MidiMessage::noteOn (1, (int) e.note, e.value));
                break;
            case UiEvent::Type::noteOff:
                synthEngine.handleMidi (juce::MidiMessage::noteOff (1, (int) e.note));
                break;
            case UiEvent::Type::previewOn:
                synthEngine.previewNoteOn ((int) e.note,
                                           (dsp::Wave) juce::jlimit (0, dsp::numWaves - 1, (int) e.wave));
                break;
            case UiEvent::Type::previewOff:
                synthEngine.previewNoteOff ((int) e.note);
                break;
            case UiEvent::Type::pitchBend:
                synthEngine.setPitchBend (e.value);
                break;
            case UiEvent::Type::allNotesOff:
                synthEngine.allNotesOff();
                break;
        }
    });

    int position = 0;

    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit (position, numSamples, metadata.samplePosition);
        renderRange (buffer, position, eventPosition - position);
        position = eventPosition;
        synthEngine.handleMidi (metadata.getMessage());
    }

    renderRange (buffer, position, numSamples - position);

    midi.clear();

    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

    float current = outputPeak.load (std::memory_order_relaxed);
    while (peak > current && ! outputPeak.compare_exchange_weak (current, peak, std::memory_order_relaxed))
    {
    }
}

juce::AudioProcessorEditor* ScratchpadProcessor::createEditor()
{
    return new ScratchpadEditor (*this);
}

bool ScratchpadProcessor::isPatchStarted() const
{
    return oscPointers[0].enabled->load (std::memory_order_relaxed) >= 0.5f;
}

void ScratchpadProcessor::pickWave (int waveIndex)
{
    const auto names = params::waveNames();
    const auto name = names[juce::jlimit (0, names.size() - 1, waveIndex)];

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Picked " + name.toLowerCase());
    setParamValue (apvts, params::osc1Wave, (float) waveIndex);
    setParamValue (apvts, params::osc1Enabled, 1.0f);
}

void ScratchpadProcessor::changeWave (int waveIndex)
{
    const auto names = params::waveNames();
    const int next = juce::jlimit (0, names.size() - 1, waveIndex);
    const int previous = juce::jlimit (0, names.size() - 1, (int) (oscPointers[0].wave->load (std::memory_order_relaxed) + 0.5f));

    if (next == previous)
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Osc 1 wave " + names[previous].toLowerCase()
                                     + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + names[next].toLowerCase());
    setParamValue (apvts, params::osc1Wave, (float) next);
}

void ScratchpadProcessor::newPatch()
{
    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("New patch");

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            setParamValue (apvts, ranged->paramID.toRawUTF8(), defaultOf (apvts, ranged->paramID.toRawUTF8()));
}

namespace
{
    /** Every parameter that belongs to `m`, including its `enabled` flag. */
    std::vector<const char*> parametersOf (Module m)
    {
        const auto oscillator = [] (size_t i)
        {
            const auto& o = params::osc[i];
            return std::vector<const char*> { o.enabled, o.wave, o.octave, o.semitone, o.fine, o.pulseWidth, o.level, o.phaseReset };
        };

        switch (m)
        {
            case Module::osc2:      return oscillator (1);
            case Module::osc3:      return oscillator (2);
            case Module::noise:     return { params::noiseEnabled, params::noiseColour, params::noiseLevel };
            case Module::filter:    return { params::filterEnabled, params::filterMode, params::filterCutoff,
                                             params::filterResonance, params::filterKeyTrack, params::filterDrive };
            case Module::ampEnv:    return { params::ampEnv.enabled, params::ampEnv.attack, params::ampEnv.decay,
                                             params::ampEnv.sustain, params::ampEnv.release, params::ampEnvVelocity };
            case Module::filterEnv: return { params::filterEnv.enabled, params::filterEnv.attack, params::filterEnv.decay,
                                             params::filterEnv.sustain, params::filterEnv.release, params::filterEnvAmount };
        }
        return {};
    }

    const char* enabledOf (Module m)
    {
        return parametersOf (m).front();
    }
}

juce::String ScratchpadProcessor::moduleName (Module m)
{
    switch (m)
    {
        case Module::osc2:      return "Oscillator 2";
        case Module::osc3:      return "Oscillator 3";
        case Module::noise:     return "Noise";
        case Module::filter:    return "Filter";
        case Module::ampEnv:    return "Amp Envelope";
        case Module::filterEnv: return "Filter Envelope";
    }
    return {};
}

bool ScratchpadProcessor::isModulePresent (Module m) const
{
    // The parameter's own value, not the cached atomic: a listener told that
    // this parameter changed runs before the atomic is updated.
    return apvts.getParameter (enabledOf (m))->getValue() >= 0.5f;
}

void ScratchpadProcessor::addModule (Module m)
{
    if (isModulePresent (m))
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Added " + moduleName (m));

    for (const char* id : parametersOf (m))
        setParamValue (apvts, id, defaultOf (apvts, id));

    if (m == Module::osc2 || m == Module::osc3)
    {
        const auto& ids = params::osc[m == Module::osc2 ? 1u : 2u];
        setParamValue (apvts, ids.wave, oscPointers[0].wave->load (std::memory_order_relaxed));
        setParamValue (apvts, ids.fine, params::newOscillatorFineCents);
    }

    setParamValue (apvts, enabledOf (m), 1.0f);
}

void ScratchpadProcessor::removeModule (Module m)
{
    if (! isModulePresent (m))
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Removed " + moduleName (m));

    for (const char* id : parametersOf (m))
        setParamValue (apvts, id, defaultOf (apvts, id));
}

void ScratchpadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    if (auto xml = state.createXml())
    {
        patch::stamp (*xml);
        copyXmlToBinary (*xml, destData);
    }
}

void ScratchpadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto result = patch::migrate (*xml, apvts.state.getType().toString());

    if (result == patch::MigrationResult::invalid || result == patch::MigrationResult::tooNew)
        return;

    // Parameters the saved state does not mention (a patch saved by an older
    // version) must come up at their defaults, not at whatever they were set to.
    for (auto* parameter : getParameters())
        parameter->setValueNotifyingHost (parameter->getDefaultValue());

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace scratchpad::plugin
