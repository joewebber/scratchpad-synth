#pragma once

#include "engine/SynthEngine.h"
#include "plugin/Modules.h"
#include "plugin/Parameters.h"
#include "plugin/UiEventQueue.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>

namespace scratchpad::plugin
{

class ScratchpadProcessor final : public juce::AudioProcessor
{
public:
    ScratchpadProcessor();
    ~ScratchpadProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scratchpad Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Patch operations (message thread). Each is one named undo step.
    bool isPatchStarted() const;
    void pickWave (int waveIndex);
    void changeWave (int waveIndex);
    void newPatch();

    /** Modules the user adds to the oscillator the picker made. Presence is
        the module's `enabled` parameter. Adding resets the module's
        parameters to their defaults (a new Osc 2 or 3 takes Osc 1's wave and
        starts detuned by 7 cents so it is audible at once); removing resets
        them too. Each is one named undo step. */
    bool isModulePresent (Module) const;
    void addModule (Module);
    void removeModule (Module);
    static juce::String moduleName (Module);

    /** Peak of the output since the last call, then resets. Message thread. */
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f, std::memory_order_relaxed); }

    UiEventQueue& getUiEvents() noexcept { return uiEvents; }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    // Standalone hooks, set by the plugin entry point. Empty in other formats.
    std::function<void()> openAudioSettings;
    std::function<bool()> audioDeviceMissing;

private:
    engine::EngineParams readParams() const noexcept;
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int count) noexcept;

    engine::SynthEngine synthEngine;
    UiEventQueue uiEvents;

    struct OscPointers
    {
        std::atomic<float> *enabled, *wave, *octave, *semitone, *fine, *pulseWidth, *level, *phaseReset;
    };

    struct EnvPointers
    {
        std::atomic<float> *enabled, *attack, *decay, *sustain, *release;
    };

    std::atomic<float>* findParam (const char* id) const;

    std::array<OscPointers, params::numOscillators> oscPointers {};
    EnvPointers ampPointers {}, filterEnvPointers {};
    std::atomic<float>* noiseEnabled = nullptr;
    std::atomic<float>* noiseColour = nullptr;
    std::atomic<float>* noiseLevel = nullptr;
    std::atomic<float>* filterEnabled = nullptr;
    std::atomic<float>* filterMode = nullptr;
    std::atomic<float>* filterCutoff = nullptr;
    std::atomic<float>* filterResonance = nullptr;
    std::atomic<float>* filterKeyTrack = nullptr;
    std::atomic<float>* filterDrive = nullptr;
    std::atomic<float>* ampVelocity = nullptr;
    std::atomic<float>* filterEnvAmount = nullptr;
    std::atomic<float>* voicingMode = nullptr;
    std::atomic<float>* voicingVoices = nullptr;
    std::atomic<float>* voicingGlide = nullptr;
    std::atomic<float>* voicingBendRange = nullptr;
    std::atomic<float>* outVolume = nullptr;

    std::atomic<float> outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchpadProcessor)
};

} // namespace scratchpad::plugin
