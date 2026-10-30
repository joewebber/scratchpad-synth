#include "plugin/PluginProcessor.h"

#include "plugin/Parameters.h"
#include "plugin/PatchDescription.h"
#include "plugin/PatchState.h"
#include "plugin/PluginEditor.h"

#include <algorithm>
#include <map>
#include <optional>

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

    float defaultOf (juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        auto* p = apvts.getParameter (id);
        return p->convertFrom0to1 (p->getDefaultValue());
    }

    const juce::Identifier& effectOrderId()
    {
        static const juce::Identifier id { params::effectOrderProperty };
        return id;
    }

    const juce::Identifier& patchNameId()
    {
        static const juce::Identifier id { patch::nameProperty };
        return id;
    }

    constexpr std::array<engine::EffectKind, engine::numEffects> defaultEffectOrder {
        engine::EffectKind::distortion, engine::EffectKind::chorus, engine::EffectKind::delay, engine::EffectKind::reverb
    };

    /** Two bits per position, position 0 lowest. The default order packs to 0xE4. */
    std::uint32_t packEffectOrder (const std::array<engine::EffectKind, engine::numEffects>& order) noexcept
    {
        std::uint32_t packed = 0;

        for (size_t i = 0; i < order.size(); ++i)
            packed |= (std::uint32_t) order[i] << (2 * i);

        return packed;
    }

    std::array<engine::EffectKind, engine::numEffects> unpackEffectOrder (std::uint32_t packed) noexcept
    {
        std::array<engine::EffectKind, engine::numEffects> order {};

        for (size_t i = 0; i < order.size(); ++i)
            order[i] = (engine::EffectKind) ((packed >> (2 * i)) & 3u);

        return order;
    }

    juce::String formatEffectOrder (const std::array<engine::EffectKind, engine::numEffects>& order)
    {
        juce::StringArray parts;

        for (const auto kind : order)
            parts.add (juce::String ((int) kind));

        return parts.joinIntoString (",");
    }

    /** "0,1,2,3" in some order; anything else is nullopt. */
    std::optional<std::array<engine::EffectKind, engine::numEffects>> parseEffectOrder (const juce::String& text)
    {
        juce::StringArray parts;
        parts.addTokens (text, ",", "");

        if (parts.size() != engine::numEffects)
            return std::nullopt;

        std::array<bool, engine::numEffects> seen {};
        std::array<engine::EffectKind, engine::numEffects> order {};

        for (int i = 0; i < engine::numEffects; ++i)
        {
            const auto part = parts[i].trim();

            if (part.length() != 1 || ! juce::CharacterFunctions::isDigit (part[0]))
                return std::nullopt;

            const int value = part.getIntValue();

            if (value >= engine::numEffects || seen[(size_t) value])
                return std::nullopt;

            seen[(size_t) value] = true;
            order[(size_t) i] = (engine::EffectKind) value;
        }

        return order;
    }

    Module effectModule (engine::EffectKind kind) noexcept
    {
        return (Module) ((int) Module::distortion + (int) kind);
    }
}


ScratchpadProcessor::~ScratchpadProcessor()
{
    apvts.state.removeListener (this);
}

ScratchpadProcessor::ScratchpadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "ScratchpadSynth", params::createLayout())
{
    for (size_t i = 0; i < oscPointers.size(); ++i)
    {
        const auto& ids = params::osc[i];
        oscPointers[i] = { findParam (ids.enabled), findParam (ids.wave), findParam (ids.octave), findParam (ids.semitone),
                           findParam (ids.fine), findParam (ids.pulseWidth), findParam (ids.level), findParam (ids.phaseReset),
                           findParam (ids.unison), findParam (ids.unisonDetune), findParam (ids.unisonSpread), findParam (ids.antiAlias),
                           ids.sync != nullptr ? findParam (ids.sync) : nullptr };
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

    static_assert (params::numModSlots == engine::numModSlots);

    for (size_t i = 0; i < bypassPointers.size(); ++i)
        bypassPointers[i] = findParam ((juce::String (params::bypassKeys[i]) + ".bypass").toRawUTF8());

    for (size_t i = 0; i < soloPointers.size(); ++i)
        soloPointers[i] = findParam ((juce::String (params::bypassKeys[i]) + ".solo").toRawUTF8());

    for (size_t i = 0; i < lfoPointers.size(); ++i)
    {
        const auto& ids = params::lfo[i];
        lfoPointers[i] = { findParam (ids.enabled), findParam (ids.shape), findParam (ids.rate),
                           findParam (ids.retrigger), findParam (ids.fade), findParam (ids.sync), findParam (ids.division) };
    }

    const auto changed = [this] (float) { modulationBroadcaster.sendChangeMessage(); };

    for (size_t i = 0; i < modPointers.size(); ++i)
    {
        const auto& ids = params::mod[i];
        modPointers[i] = { findParam (ids.source), findParam (ids.dest), findParam (ids.amount) };
        modParams[i] = { dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ids.source)),
                         dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ids.dest)),
                         apvts.getParameter (ids.amount) };

        amountAttachments[i] = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (ids.amount), changed, &undoManager);
        choiceWatchers[2 * i] = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (ids.source), changed);
        choiceWatchers[2 * i + 1] = std::make_unique<juce::ParameterAttachment> (*apvts.getParameter (ids.dest), changed);
    }

    fx = { findParam (params::distortionEnabled), findParam (params::distortionDrive), findParam (params::distortionTone), findParam (params::distortionMix),
           findParam (params::chorusEnabled), findParam (params::chorusRate), findParam (params::chorusDepth), findParam (params::chorusMix),
           findParam (params::delayEnabled), findParam (params::delayTime), findParam (params::delaySync), findParam (params::delayDivision),
           findParam (params::delayFeedback), findParam (params::delayPingPong), findParam (params::delayMix),
           findParam (params::reverbEnabled), findParam (params::reverbSize), findParam (params::reverbDecay), findParam (params::reverbDamping),
           findParam (params::reverbMix),
           findParam (params::ringLevel), findParam (params::fmAmount), findParam (params::fmTarget) };

    apvts.state.addListener (this);
    refreshEffectOrder();
}

std::atomic<float>* ScratchpadProcessor::findParam (const char* id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}

void ScratchpadProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
    synthEngine.prepare (sampleRate);
    synthEngine.setParams (readParams());

    // The band-limited oscillators and the oversampled distortion run a fixed number of samples late.
    setLatencySamples (dsp::Oscillator::latencySamples + synthEngine.getEffectsLatencySamples());
}

double tailLengthSeconds (const engine::EngineParams& p) noexcept
{
    // Slack for the gate's 2 ms ramp, a stolen voice's 5 ms fade, the oscillators' 16 samples and the distortion's delay.
    constexpr double slack = 0.02;
    constexpr double longestPart = 20.0; // no single part (the delay's repeats, the release) is reported longer

    // The voice: the amp envelope's release, else only the declick gate.
    double seconds = p.ampEnv.enabled ? juce::jmin ((double) p.ampEnv.release, longestPart) : 0.0;

    // Effects run in series, so a tail that enters the next effect leaves it later: the tails add.
    if (p.reverb.enabled)
    {
        // `decaySeconds` is the time for the tail to fall 60 dB; the longest of the reverb's eight lines adds its own length.
        constexpr double longestLine = 0.0663 * 2.0; // 66.3 ms at the largest size (see Reverb.h)
        seconds += juce::jmin ((double) p.reverb.decaySeconds, longestPart) + longestLine;
    }

    if (p.delay.enabled)
    {
        // The n-th echo has fed back n - 1 times: it is 60 dB down once feedback^(n - 1) <= 0.001. Feedback is capped at
        // 0.95 by the parameter, and anything near 1 is held to `longestPart` rather than reported as endless.
        const double time = (double) p.delay.timeMs * 0.001;
        const double feedback = juce::jlimit (0.0, 0.999, (double) p.delay.feedback);
        const double repeats = feedback < 1.0e-3 ? 0.0 : std::ceil (std::log (0.001) / std::log (feedback));
        seconds += juce::jmin ((1.0 + repeats) * time, longestPart);
    }

    if (p.chorus.enabled)
        seconds += 0.03; // its longest delay, about 12 ms centre plus sweep

    return seconds + slack;
}

double ScratchpadProcessor::getTailLengthSeconds() const
{
    // Cached parameter atomics only: hosts may call this from any thread.
    return tailLengthSeconds (readParams());
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
    const double bpm = currentBpm.load (std::memory_order_relaxed);

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
        o.unison = juce::jlimit (1, params::maxUnison, juce::roundToInt (load (s.unison)));
        o.unisonDetuneCents = load (s.unisonDetune);
        o.unisonSpread = fraction (s.unisonSpread);
        o.antiAlias = on (s.antiAlias);
        o.sync = s.sync != nullptr && on (s.sync);
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

    for (size_t i = 0; i < lfoPointers.size(); ++i)
    {
        const auto& s = lfoPointers[i];
        auto& l = e.lfo[i];
        l.enabled = on (s.enabled);
        l.shape = (dsp::LfoShape) index (s.shape, dsp::numLfoShapes);
        l.rateHz = on (s.sync) ? params::lfoSyncRateHz (bpm, index (s.division, (int) params::tempoDivisionBeats.size())) : load (s.rate);
        l.retrigger = on (s.retrigger);
        l.fadeSeconds = load (s.fade);
    }

    for (size_t i = 0; i < modPointers.size(); ++i)
    {
        const auto& s = modPointers[i];
        e.mod[i] = { (engine::ModSource) index (s.source, engine::numModSources),
                     (engine::ModDest) index (s.dest, engine::numModDests),
                     juce::jlimit (-1.0f, 1.0f, fraction (s.amount)) };
    }

    e.voicing.mode = (engine::VoiceMode) index (voicingMode, 3);
    e.voicing.voices = juce::jlimit (1, params::maxVoices, juce::roundToInt (load (voicingVoices)));
    e.voicing.glideSeconds = load (voicingGlide);
    e.voicing.bendRange = load (voicingBendRange);

    e.ringLevelDb = load (fx.ringLevel);
    e.fmAmount = fraction (fx.fmAmount);
    e.fmTarget = (engine::FmTarget) index (fx.fmTarget, 3);

    e.distortion.enabled = on (fx.distortionEnabled);
    e.distortion.driveDb = load (fx.distortionDrive);
    e.distortion.tone = fraction (fx.distortionTone);
    e.distortion.mix = fraction (fx.distortionMix);

    e.chorus.enabled = on (fx.chorusEnabled);
    e.chorus.rateHz = load (fx.chorusRate);
    e.chorus.depth = fraction (fx.chorusDepth);
    e.chorus.mix = fraction (fx.chorusMix);

    e.delay.enabled = on (fx.delayEnabled);
    e.delay.timeMs = on (fx.delaySync) ? params::delaySyncMs (bpm, index (fx.delayDivision, (int) params::tempoDivisionBeats.size()))
                                       : load (fx.delayTime);
    e.delay.feedback = fraction (fx.delayFeedback);
    e.delay.pingPong = on (fx.delayPingPong);
    e.delay.mix = fraction (fx.delayMix);

    e.reverb.enabled = on (fx.reverbEnabled);
    e.reverb.size = fraction (fx.reverbSize);
    e.reverb.decaySeconds = load (fx.reverbDecay);
    e.reverb.damping = fraction (fx.reverbDamping);
    e.reverb.mix = fraction (fx.reverbMix);

    e.outputVolumeDb = load (outVolume);

    // Bypass and solo are applied here, so the engine only ever sees what is audible. A muted source keeps
    // its `enabled` flag: losing oscillator 1 would release the held notes.
    const auto bypassed = [&] (size_t i) noexcept { return on (bypassPointers[i]); };
    bool anySolo = false;
    for (const auto* s : soloPointers)
        anySolo = anySolo || on (s);

    const auto muted = [&] (size_t i) noexcept { return bypassed (i) || (anySolo && ! on (soloPointers[i])); };

    for (size_t i = 0; i < e.osc.size(); ++i)
        if (muted (i))
            e.osc[i].levelDb = engine::minLevelDb;

    if (muted (3))
        e.noise.levelDb = engine::minLevelDb;

    e.filter.enabled = e.filter.enabled && ! bypassed (4);
    e.ampEnv.enabled = e.ampEnv.enabled && ! bypassed (5);
    e.filterEnv.enabled = e.filterEnv.enabled && ! bypassed (6);
    e.lfo[0].enabled = e.lfo[0].enabled && ! bypassed (7);
    e.lfo[1].enabled = e.lfo[1].enabled && ! bypassed (8);

    // Ring modulation is a source of oscillators 1 and 2: it goes quiet when either is muted. FM needs oscillator 1 playing.
    if (muted (0) || muted (1))
        e.ringLevelDb = engine::minLevelDb;

    if (bypassed (0))
        e.fmAmount = 0.0f;

    // Effects: a bypassed effect is simply absent. Tempo sync is resolved here, so the engine only sees milliseconds.
    constexpr size_t firstEffect = (size_t) (params::numBypassable - params::numEffectKinds);
    e.distortion.enabled = e.distortion.enabled && ! bypassed (firstEffect);
    e.chorus.enabled = e.chorus.enabled && ! bypassed (firstEffect + 1);
    e.delay.enabled = e.delay.enabled && ! bypassed (firstEffect + 2);
    e.reverb.enabled = e.reverb.enabled && ! bypassed (firstEffect + 3);

    const auto packed = packedEffectOrder.load (std::memory_order_relaxed);
    e.effectOrder = unpackEffectOrder (packed);
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

    {
        // Tempo: the host's when it says, else 120. Read before the block's parameters so sync uses this block's tempo.
        double bpm = params::defaultBpm;

        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
                if (const auto hostBpm = position->getBpm())
                    bpm = *hostBpm;

        currentBpm.store (bpm > 0.0 ? bpm : params::defaultBpm, std::memory_order_relaxed);
    }

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
            case UiEvent::Type::pitchBend:
                synthEngine.setPitchBend (e.value);
                break;
            case UiEvent::Type::modWheel:
                synthEngine.setModWheel (e.value);
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
        // The engine only acts on channel messages (three bytes at most). Copying anything longer (SysEx, which a
        // host may send to any instrument) into a MidiMessage would allocate on the audio thread.
        if (metadata.numBytes <= 3)
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

void ScratchpadProcessor::setParamValue (const char* id, float unnormalisedValue)
{
    // Goes through the ValueTree with the undo manager, so the change is
    // recorded in the current transaction straight away.
    auto v = apvts.getParameterAsValue (id);
    v = unnormalisedValue;
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
    setParamValue (params::osc1Wave, (float) next);
}

void ScratchpadProcessor::newPatch()
{
    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("New patch");

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            setParamValue (ranged->paramID.toRawUTF8(), defaultOf (apvts, ranged->paramID.toRawUTF8()));

    if (getEffectOrder() != defaultEffectOrder)
        setEffectOrderProperty (defaultEffectOrder);

    apvts.state.removeProperty (patchNameId(), &undoManager);
}

juce::File ScratchpadProcessor::getPatchesDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Scratchpad Synth")
        .getChildFile ("Patches");
}

juce::String ScratchpadProcessor::getPatchName() const
{
    const auto name = apvts.state.getProperty (patchNameId()).toString();
    return name.isEmpty() ? juce::String (patch::untitledName) : name;
}

bool ScratchpadProcessor::savePatch (const juce::File& file)
{
    if (file.isDirectory())
        return false;

    const auto name = file.getFileNameWithoutExtension();

    // The file carries the name it is saved under; the live patch only takes it once the write has worked.
    auto state = apvts.copyState();
    state.setProperty (patchNameId(), name, nullptr);

    auto xml = state.createXml();

    if (xml == nullptr || ! file.getParentDirectory().createDirectory().wasOk())
        return false;

    patch::stamp (*xml);

    // Written beside the target and swapped in, so a failed or interrupted save never leaves half a patch.
    juce::TemporaryFile temporary (file);

    if (! xml->writeTo (temporary.getFile()) || ! temporary.overwriteTargetFileWithTemporary())
        return false;

    // The name describes where the patch lives, not an edit to it, so it is not an undo step.
    apvts.state.setProperty (patchNameId(), name, nullptr);
    return true;
}

bool ScratchpadProcessor::loadPatch (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    // The parser forgives a missing closing tag; a patch cut off by a full disk or an interrupted copy must not
    // open as a half-loaded one.
    juce::XmlDocument document (file);
    auto xml = document.getDocumentElement();

    // Every file we write carries a stamp, as the last attribute of its first tag, so a file without one is not
    // ours or was cut off inside that tag. Only host sessions can be unstamped (M1's were).
    if (xml == nullptr || document.getLastParseError().isNotEmpty() || ! xml->hasAttribute (patch::schemaVersionAttribute))
        return false;

    const auto result = patch::migrate (*xml, apvts.state.getType().toString());

    if (result == patch::MigrationResult::invalid || result == patch::MigrationResult::tooNew)
        return false;

    const auto tree = juce::ValueTree::fromXml (*xml);
    const auto name = file.getFileNameWithoutExtension();

    std::map<juce::String, float> saved;

    for (const auto child : tree)
        if (child.hasType ("PARAM") && child.hasProperty ("id") && child.hasProperty ("value"))
            saved[child["id"].toString()] = (float) (double) child["value"];

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Opened " + name);

    // Every parameter goes through the value tree with the undo manager, so the whole patch is one step. Those
    // the file does not mention take their defaults, as in a host session.
    for (auto* p : getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = ranged->paramID.toRawUTF8();
            const auto found = saved.find (ranged->paramID);
            const auto target = found != saved.end() ? found->second : defaultOf (apvts, id);

            // One already there is left alone, so the undo step holds only what the file really changes.
            if (std::abs (ranged->getValue() - ranged->convertTo0to1 (target)) > 1.0e-6f)
                setParamValue (id, target);
        }
    }

    // Every patch has oscillator 1, even one saved before the wave picker went away.
    setParamValue (params::osc1Enabled, 1.0f);

    const auto order = parseEffectOrder (tree.getProperty (effectOrderId()).toString()).value_or (defaultEffectOrder);

    if (getEffectOrder() != order)
        setEffectOrderProperty (order);

    apvts.state.setProperty (patchNameId(), name, &undoManager);
    return true;
}

namespace
{
    /** Every parameter that belongs to `m`, including its `enabled` flag. */
    std::vector<const char*> parametersOf (Module m)
    {
        const auto oscillator = [] (size_t i)
        {
            const auto& o = params::osc[i];
            std::vector<const char*> ids { o.enabled, o.wave, o.octave, o.semitone, o.fine, o.pulseWidth, o.level, o.phaseReset,
                                           o.unison, o.unisonDetune, o.unisonSpread, o.antiAlias };

            if (o.sync != nullptr)
                ids.push_back (o.sync);

            return ids;
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
            case Module::lfo1:
            case Module::lfo2:
            {
                const auto& l = params::lfo[m == Module::lfo1 ? 0u : 1u];
                return { l.enabled, l.shape, l.rate, l.retrigger, l.fade, l.sync, l.division };
            }
            case Module::distortion: return { params::distortionEnabled, params::distortionDrive, params::distortionTone, params::distortionMix };
            case Module::chorus:     return { params::chorusEnabled, params::chorusRate, params::chorusDepth, params::chorusMix };
            case Module::delay:      return { params::delayEnabled, params::delayTime, params::delaySync, params::delayDivision,
                                              params::delayFeedback, params::delayPingPong, params::delayMix };
            case Module::reverb:     return { params::reverbEnabled, params::reverbSize, params::reverbDecay, params::reverbDamping, params::reverbMix };
        }
        return {};
    }

    juce::String moduleKey (Module m)
    {
        switch (m)
        {
            case Module::osc2:       return "osc2";
            case Module::osc3:       return "osc3";
            case Module::noise:      return "noise";
            case Module::filter:     return "filter";
            case Module::ampEnv:     return "ampEnv";
            case Module::filterEnv:  return "filterEnv";
            case Module::lfo1:       return "lfo1";
            case Module::lfo2:       return "lfo2";
            case Module::distortion: return "distortion";
            case Module::chorus:     return "chorus";
            case Module::delay:      return "delay";
            case Module::reverb:     return "reverb";
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
        case Module::osc2:       return "Oscillator 2";
        case Module::osc3:       return "Oscillator 3";
        case Module::noise:      return "Noise";
        case Module::filter:     return "Filter";
        case Module::ampEnv:     return "Amp Envelope";
        case Module::filterEnv:  return "Filter Envelope";
        case Module::lfo1:       return "LFO 1";
        case Module::lfo2:       return "LFO 2";
        case Module::distortion: return "Distortion";
        case Module::chorus:     return "Chorus";
        case Module::delay:      return "Delay";
        case Module::reverb:     return "Reverb";
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
        setParamValue (id, defaultOf (apvts, id));

    if (m == Module::osc2 || m == Module::osc3)
    {
        const auto& ids = params::osc[m == Module::osc2 ? 1u : 2u];
        setParamValue (ids.wave, oscPointers[0].wave->load (std::memory_order_relaxed));
        setParamValue (ids.fine, params::newOscillatorFineCents);
    }

    setParamValue (enabledOf (m), 1.0f);
}

void ScratchpadProcessor::removeModule (Module m)
{
    if (! isModulePresent (m))
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Removed " + moduleName (m));

    for (const char* id : parametersOf (m))
        setParamValue (id, defaultOf (apvts, id));

    for (const auto& suffix : { ".bypass", ".solo" })
    {
        const auto id = moduleKey (m) + suffix;

        if (apvts.getParameter (id) != nullptr)
            setParamValue (id.toRawUTF8(), 0.0f);
    }

    // Connections that start or end at the module have nothing left to talk to.
    const auto connections = getModConnections();

    for (size_t i = 0; i < connections.size(); ++i)
        if (modulation::moduleOf (connections[i].source) == m || modulation::moduleOf (connections[i].dest) == m)
            clearModSlot ((int) i);
}

//==============================================================================
engine::ModConnections ScratchpadProcessor::getModConnections() const
{
    engine::ModConnections out;

    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto& p = modParams[i];
        out[i] = { (engine::ModSource) juce::jlimit (0, engine::numModSources - 1, p.source->getIndex()),
                   (engine::ModDest) juce::jlimit (0, engine::numModDests - 1, p.dest->getIndex()),
                   p.amount->convertFrom0to1 (p.amount->getValue()) * 0.01f };
    }

    return out;
}

bool ScratchpadProcessor::isModSourcePresent (engine::ModSource s) const
{
    if (s == engine::ModSource::none)
        return false;

    const auto owner = modulation::moduleOf (s);
    return ! owner.has_value() || isModulePresent (*owner);
}

bool ScratchpadProcessor::isModDestinationPresent (engine::ModDest d) const
{
    if (d == engine::ModDest::none)
        return false;

    const auto owner = modulation::moduleOf (d);
    return ! owner.has_value() || isModulePresent (*owner);
}

ScratchpadProcessor::ConnectResult ScratchpadProcessor::connectMod (engine::ModSource source, engine::ModDest destination)
{
    const bool selfRate = (source == engine::ModSource::lfo1 && destination == engine::ModDest::lfo1Rate)
                       || (source == engine::ModSource::lfo2 && destination == engine::ModDest::lfo2Rate);

    if (selfRate || ! isModSourcePresent (source) || ! isModDestinationPresent (destination))
        return ConnectResult::unavailable;

    const auto connections = getModConnections();
    int freeSlot = -1;

    for (size_t i = 0; i < connections.size(); ++i)
    {
        if (connections[i].source == source && connections[i].dest == destination)
            return ConnectResult::alreadyConnected;

        if (freeSlot < 0 && ! connections[i].active())
            freeSlot = (int) i;
    }

    if (freeSlot < 0)
        return ConnectResult::noFreeSlot;

    const auto& ids = params::mod[(size_t) freeSlot];
    engine::ModConnection created { source, destination, params::modAmountDefault * 0.01f };

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Connected " + modulation::connectionText (created));
    setParamValue (ids.source, (float) (int) source);
    setParamValue (ids.dest, (float) (int) destination);
    setParamValue (ids.amount, params::modAmountDefault);
    return ConnectResult::created;
}

void ScratchpadProcessor::clearModSlot (int slot)
{
    const auto& ids = params::mod[(size_t) slot];
    setParamValue (ids.source, 0.0f);
    setParamValue (ids.dest, 0.0f);
    setParamValue (ids.amount, defaultOf (apvts, ids.amount));
}

void ScratchpadProcessor::disconnectMod (int slot)
{
    if (slot < 0 || slot >= params::numModSlots)
        return;

    const auto connection = getModConnections()[(size_t) slot];

    if (! connection.active())
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ("Removed " + modulation::connectionText (connection));
    clearModSlot (slot);
}

void ScratchpadProcessor::beginModAmountEdit (int slot)
{
    if (slot < 0 || slot >= params::numModSlots)
        return;

    amountAtEditStart[(size_t) slot] = getModConnections()[(size_t) slot].amount;
    flushPendingChanges (apvts);
    amountAttachments[(size_t) slot]->beginGesture();
}

void ScratchpadProcessor::setModAmount (int slot, float percent)
{
    if (slot >= 0 && slot < params::numModSlots)
        amountAttachments[(size_t) slot]->setValueAsPartOfGesture (juce::jlimit (-100.0f, 100.0f, percent));
}

void ScratchpadProcessor::endModAmountEdit (int slot)
{
    if (slot < 0 || slot >= params::numModSlots)
        return;

    amountAttachments[(size_t) slot]->endGesture();

    auto connection = getModConnections()[(size_t) slot];
    const float before = amountAtEditStart[(size_t) slot];

    if (std::abs (connection.amount - before) > 1.0e-4f)
    {
        connection.amount = before;
        undoManager.setCurrentTransactionName (modulation::connectionText (connection)
                                               + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))
                                               + modulation::amountText (getModConnections()[(size_t) slot].amount));
    }
}

//==============================================================================
juce::String ScratchpadProcessor::describePatch() const
{
    return describe (readParams());
}

bool ScratchpadProcessor::isBypassed (const juce::String& key) const
{
    return apvts.getParameter (key + ".bypass")->getValue() >= 0.5f;
}

bool ScratchpadProcessor::isSoloed (const juce::String& key) const
{
    auto* p = apvts.getParameter (key + ".solo");
    return p != nullptr && p->getValue() >= 0.5f;
}

namespace
{
    juce::String moduleLabel (const juce::String& key)
    {
        if (key.startsWith ("osc"))  return "Osc " + key.substring (3);
        if (key == "noise")          return "Noise";
        if (key == "filter")         return "Filter";
        if (key == "ampEnv")         return "Amp Envelope";
        if (key == "filterEnv")      return "Filter Envelope";

        for (size_t i = 0; i < params::effectKeys.size(); ++i)
            if (key == params::effectKeys[i])
                return ScratchpadProcessor::moduleName (effectModule ((engine::EffectKind) i));

        return "LFO " + key.substring (3);
    }
}

void ScratchpadProcessor::setBypassed (const juce::String& key, bool shouldBypass)
{
    if (isBypassed (key) == shouldBypass)
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ((shouldBypass ? "Bypassed " : "Un-bypassed ") + moduleLabel (key));
    setParamValue ((key + ".bypass").toRawUTF8(), shouldBypass ? 1.0f : 0.0f);
}

void ScratchpadProcessor::setSoloed (const juce::String& key, bool shouldSolo)
{
    if (isSoloed (key) == shouldSolo)
        return;

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction ((shouldSolo ? "Soloed " : "Un-soloed ") + moduleLabel (key));

    if (shouldSolo)
        for (int i = 0; i < params::numSoloable; ++i)
            if (juce::String (params::bypassKeys[(size_t) i]) != key && isSoloed (params::bypassKeys[(size_t) i]))
                setParamValue ((juce::String (params::bypassKeys[(size_t) i]) + ".solo").toRawUTF8(), 0.0f);

    setParamValue ((key + ".solo").toRawUTF8(), shouldSolo ? 1.0f : 0.0f);
}

//==============================================================================
std::array<engine::EffectKind, engine::numEffects> ScratchpadProcessor::getEffectOrder() const
{
    return unpackEffectOrder (packedEffectOrder.load (std::memory_order_relaxed));
}

void ScratchpadProcessor::setEffectOrderProperty (const std::array<engine::EffectKind, engine::numEffects>& order)
{
    apvts.state.setProperty (effectOrderId(), formatEffectOrder (order), &undoManager);
}

void ScratchpadProcessor::refreshEffectOrder()
{
    const auto parsed = parseEffectOrder (apvts.state.getProperty (effectOrderId()).toString());
    packedEffectOrder.store (packEffectOrder (parsed.value_or (defaultEffectOrder)), std::memory_order_relaxed);
    effectsBroadcaster.sendChangeMessage();
}

void ScratchpadProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == apvts.state && property == effectOrderId())
        refreshEffectOrder();
    else if (tree == apvts.state && property == patchNameId())
        patchNameBroadcaster.sendChangeMessage();
}

void ScratchpadProcessor::valueTreeRedirected (juce::ValueTree& tree)
{
    if (tree == apvts.state)
    {
        refreshEffectOrder();
        patchNameBroadcaster.sendChangeMessage();
    }
}

void ScratchpadProcessor::moveEffect (int fromPosition, int toPosition)
{
    constexpr int n = engine::numEffects;

    if (fromPosition < 0 || fromPosition >= n || toPosition < 0 || toPosition >= n || fromPosition == toPosition)
        return;

    auto order = getEffectOrder();
    const auto moved = order[(size_t) fromPosition];

    if (fromPosition < toPosition)
        std::rotate (order.begin() + fromPosition, order.begin() + fromPosition + 1, order.begin() + toPosition + 1);
    else
        std::rotate (order.begin() + toPosition, order.begin() + fromPosition, order.begin() + fromPosition + 1);

    const auto name = moduleName (effectModule (moved));
    const auto description = toPosition + 1 < n
                               ? "Moved " + name + " before " + moduleName (effectModule (order[(size_t) toPosition + 1]))
                               : "Moved " + name + " after " + moduleName (effectModule (order[(size_t) toPosition - 1]));

    flushPendingChanges (apvts);
    undoManager.beginNewTransaction (description);
    setEffectOrderProperty (order);
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

    // A patch saved before the wave picker went away may have no oscillator 1; every patch has one now.
    apvts.getParameter (params::osc1Enabled)->setValueNotifyingHost (1.0f);
    refreshEffectOrder();
}

} // namespace scratchpad::plugin
