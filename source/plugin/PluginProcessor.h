#pragma once

#include "engine/SynthEngine.h"
#include "plugin/Modules.h"
#include "plugin/ModulationInfo.h"
#include "plugin/Parameters.h"
#include "plugin/UiEventQueue.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <cstdint>
#include <memory>

namespace scratchpad::plugin
{

/** An upper bound, in seconds, on how long the sound goes on after the last note-off for this patch: the amp envelope's
    release (else a few milliseconds of gate), plus the tails of the effects that are present and not bypassed (the
    reverb's decay time, the delay's repeats until they are 60 dB down, a little for the chorus). Each part is held to 20 s. */
double tailLengthSeconds (const engine::EngineParams&) noexcept;

class ScratchpadProcessor final : public juce::AudioProcessor,
                                  private juce::ValueTree::Listener
{
public:
    ScratchpadProcessor();
    ~ScratchpadProcessor() override;

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
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Current patch"; } // the VST3 validator rejects a program with no name
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Patch operations (message thread). Each is one named undo step.
    void changeWave (int waveIndex);
    void newPatch();

    /** Patch files (message thread). The file is the host's state XML, so a saved patch is exactly what a session
        would hold; see `patch::fileExtension` and `docs/architecture.md`.

        `savePatch` writes the patch to `file` (creating its folder), names the patch after the file's stem and
        returns false, changing nothing, if the file cannot be written.

        `loadPatch` replaces the whole patch with the file's, as one undo step named "Opened <name>", and names
        it after the file's stem. Parameters the file does not mention come up at their defaults, like a host
        session. It returns false and leaves the current patch alone for a missing or unreadable file, XML that
        is not a Scratchpad patch, or a patch saved by a newer version. */
    bool savePatch (const juce::File& file);
    bool loadPatch (const juce::File& file);

    /** `<Documents>/Scratchpad Synth/Patches`, where the Save and Open dialogs start. Not created until a save. */
    static juce::File getPatchesDirectory();

    /** The patch's name: the stem of the file it was last saved or opened as, or "Untitled patch". New patch resets
        it, and undo restores it. The broadcaster fires after every change to it, by any route. */
    juce::String getPatchName() const;
    juce::ChangeBroadcaster& getPatchNameBroadcaster() noexcept { return patchNameBroadcaster; }

    /** Modules the user adds to oscillator 1, which every patch has. Presence is
        the module's `enabled` parameter. Adding resets the module's
        parameters to their defaults (a new Osc 2 or 3 takes Osc 1's wave and
        starts detuned by 7 cents so it is audible at once); removing resets
        them too. Each is one named undo step. */
    bool isModulePresent (Module) const;
    void addModule (Module);
    void removeModule (Module);
    static juce::String moduleName (Module);

    // Modulation (message thread). A connection is one of eight slots: a source, a
    // destination and an amount. Every change is one named undo step.
    enum class ConnectResult
    {
        created,
        alreadyConnected, // that source already drives that destination
        noFreeSlot,
        unavailable       // the source's or the destination's module is not in the patch
    };

    engine::ModConnections getModConnections() const;
    bool isModSourcePresent (engine::ModSource) const;
    bool isModDestinationPresent (engine::ModDest) const;

    /** Connects `source` to `destination` in the first free slot at +50%. */
    ConnectResult connectMod (engine::ModSource source, engine::ModDest destination);
    void disconnectMod (int slot);

    /** Edits a slot's amount (percent, -100 to +100) as one undo step, however many calls it takes. */
    void beginModAmountEdit (int slot);
    void setModAmount (int slot, float percent);
    void endModAmountEdit (int slot);

    /** Told whenever any slot's source, destination or amount changes, by any route (undo, load, host). */
    juce::ChangeBroadcaster& getModulationBroadcaster() noexcept { return modulationBroadcaster; }

    /** What the audio thread last saw: source values and LFO phases of the newest note. */
    const engine::LiveModulation& getLiveModulation() const noexcept { return synthEngine.getLiveModulation(); }

    /** Bypass and solo, by module key ("osc1", "filter", "lfo2": see `params::bypassKeys`). Bypass takes a
        module out of the sound without losing its settings; solo (sources only) mutes the other sources.
        Soloing one source un-solos the rest. Each call is one named undo step. */
    bool isBypassed (const juce::String& key) const;
    bool isSoloed (const juce::String& key) const;
    void setBypassed (const juce::String& key, bool shouldBypass);
    void setSoloed (const juce::String& key, bool shouldSolo);

    /** The patch as a sentence (see `describe`), built from what is audible right now. Message thread. */
    juce::String describePatch() const;

    /** Scope: which signal it shows, and the samples queued since the last read. Message thread. */
    void setScopeTap (engine::ScopeTap t) noexcept { synthEngine.setScopeTap (t); }
    int readScope (float* dest, int maxSamples) noexcept { return synthEngine.readScope (dest, maxSamples); }
    double getSampleRate() const noexcept { return currentSampleRate.load(); }

    /** Peak of the output since the last call, then resets. Message thread. */
    float takeOutputPeak() noexcept { return outputPeak.exchange (0.0f, std::memory_order_relaxed); }

    UiEventQueue& getUiEvents() noexcept { return uiEvents; }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;

    /** Effects (message thread). `getEffectOrder()` lists the effects first to last; positions are indices into it.
        `moveEffect` is one named undo step ("Moved Delay before Chorus") and keeps absent effects' slots: the order
        is a patch property, not a presence. `getEffectsBroadcaster()` fires after every change to the order, by any
        route (move, undo, new patch, loaded state). */
    std::array<engine::EffectKind, engine::numEffects> getEffectOrder() const;
    void moveEffect (int fromPosition, int toPosition);
    juce::ChangeBroadcaster& getEffectsBroadcaster() noexcept { return effectsBroadcaster; }

    /** The safety limiter's smoothed gain reduction in dB (0 = not touching the signal). */
    float getLimiterReductionDb() const noexcept { return synthEngine.getLimiterReductionDb(); }

    /** The tempo last used for tempo sync: the host's, or 120 when there is none. */
    double getBpm() const noexcept { return currentBpm.load (std::memory_order_relaxed); }

    // Standalone hooks, set by the plugin entry point. Empty in other formats.
    std::function<void()> openAudioSettings;
    std::function<bool()> audioDeviceMissing;

private:
    engine::EngineParams readParams() const noexcept;
    void clearModSlot (int slot);

    /** Writes a parameter's value into the state tree as part of the current undo transaction (see the constructor). */
    void setParamValue (const char* id, float unnormalisedValue);
    void renderRange (juce::AudioBuffer<float>& buffer, int start, int count) noexcept;

    engine::SynthEngine synthEngine;
    UiEventQueue uiEvents;

    struct OscPointers
    {
        std::atomic<float> *enabled, *wave, *octave, *semitone, *fine, *pulseWidth, *level, *phaseReset;
        std::atomic<float> *unison, *unisonDetune, *unisonSpread, *antiAlias, *subLevel, *sync; // sync: null for oscillator 1
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
    std::atomic<float>* voiceShaperEnabled = nullptr;
    std::atomic<float>* voiceShaperMode = nullptr;
    std::atomic<float>* voiceShaperDrive = nullptr;
    std::atomic<float>* voiceShaperBias = nullptr;
    std::atomic<float>* voiceShaperMix = nullptr;
    std::atomic<float>* ampVelocity = nullptr;
    std::atomic<float>* filterEnvAmount = nullptr;
    std::atomic<float>* voicingMode = nullptr;
    std::atomic<float>* voicingVoices = nullptr;
    std::atomic<float>* voicingGlide = nullptr;
    std::atomic<float>* voicingBendRange = nullptr;
    std::atomic<float>* voicingDrift = nullptr;
    std::atomic<float>* outVolume = nullptr;

    struct LfoPointers
    {
        std::atomic<float> *enabled, *shape, *rate, *retrigger, *fade, *sync, *division;
    };

    struct ModPointers
    {
        std::atomic<float> *source, *dest, *amount;
    };

    std::array<LfoPointers, params::numLfos> lfoPointers {};
    std::array<ModPointers, params::numModSlots> modPointers {};

    struct EffectPointers
    {
        std::atomic<float> *distortionEnabled, *distortionDrive, *distortionTone, *distortionMix, *distortionMode, *distortionBias;
        std::atomic<float> *chorusEnabled, *chorusRate, *chorusDepth, *chorusMix;
        std::atomic<float> *delayEnabled, *delayTime, *delaySync, *delayDivision, *delayFeedback, *delayPingPong, *delayMix;
        std::atomic<float> *reverbEnabled, *reverbSize, *reverbDecay, *reverbDamping, *reverbMix;
        std::atomic<float> *ringLevel, *fmAmount, *fmTarget;
    };

    EffectPointers fx {};

    struct ModParams
    {
        juce::AudioParameterChoice *source, *dest;
        juce::RangedAudioParameter* amount;
    };

    std::array<ModParams, params::numModSlots> modParams {};

    // Watch every slot parameter so the editor hears about changes made by undo,
    // a loaded session or the host. The amount ones also carry the undo manager,
    // so a drag on a modulation ring is one step.
    std::array<std::unique_ptr<juce::ParameterAttachment>, params::numModSlots> amountAttachments;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 2 * params::numModSlots> choiceWatchers;
    juce::ChangeBroadcaster modulationBroadcaster;
    std::array<float, params::numModSlots> amountAtEditStart {};

    std::array<std::atomic<float>*, params::numBypassable> bypassPointers {};
    std::array<std::atomic<float>*, params::numSoloable> soloPointers {};
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<double> currentBpm { params::defaultBpm };

    std::atomic<float> outputPeak { 0.0f };

    // Effect order. The tree property `effectOrder` is the saved truth; the audio thread only sees this packed copy
    // (two bits per position, position 0 in the lowest bits), refreshed by `valueTreePropertyChanged` and `valueTreeRedirected`.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    void refreshEffectOrder();
    void setEffectOrderProperty (const std::array<engine::EffectKind, engine::numEffects>&);

    std::atomic<std::uint32_t> packedEffectOrder { 0xE4u };
    juce::ChangeBroadcaster effectsBroadcaster;
    juce::ChangeBroadcaster patchNameBroadcaster;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchpadProcessor)
};

} // namespace scratchpad::plugin
