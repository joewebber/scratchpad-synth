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
    oscEnabled = apvts.getRawParameterValue (params::osc1Enabled);
    oscWave    = apvts.getRawParameterValue (params::osc1Wave);
    oscLevel   = apvts.getRawParameterValue (params::osc1Level);
    outVolume  = apvts.getRawParameterValue (params::outVolume);
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
    engine::EngineParams p;
    p.oscEnabled = oscEnabled->load (std::memory_order_relaxed) >= 0.5f;
    p.wave = (dsp::Wave) juce::jlimit (0, dsp::numWaves - 1, (int) (oscWave->load (std::memory_order_relaxed) + 0.5f));
    p.oscLevelDb = oscLevel->load (std::memory_order_relaxed);
    p.outputVolumeDb = outVolume->load (std::memory_order_relaxed);
    return p;
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
    return oscEnabled->load (std::memory_order_relaxed) >= 0.5f;
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
    const int previous = juce::jlimit (0, names.size() - 1, (int) (oscWave->load (std::memory_order_relaxed) + 0.5f));

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

    setParamValue (apvts, params::osc1Enabled, 0.0f);
    setParamValue (apvts, params::osc1Wave, defaultOf (apvts, params::osc1Wave));
    setParamValue (apvts, params::osc1Level, defaultOf (apvts, params::osc1Level));
    setParamValue (apvts, params::outVolume, defaultOf (apvts, params::outVolume));
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

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace scratchpad::plugin
