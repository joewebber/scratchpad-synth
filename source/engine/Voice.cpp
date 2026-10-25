#include "engine/Voice.h"

#include "dsp/FloatCompare.h"

#include <algorithm>
#include <cmath>

namespace scratchpad::engine
{

void Voice::prepare (double sr, std::uint32_t noiseSeed) noexcept
{
    sampleRate = sr;
    gate.setSampleRate (sr);
    ampEnv.setSampleRate (sr);
    filterEnv.setSampleRate (sr);
    filter.prepare (sr);
    filterR.prepare (sr);
    noise.seed (noiseSeed);

    for (size_t i = 0; i < lfos.size(); ++i)
    {
        lfos[i].setSampleRate (sr);
        lfos[i].seed (noiseSeed * 2654435761u + (std::uint32_t) i * 40503u + 1u);
    }

    if (oscillators == nullptr)
        oscillators = std::make_unique<Oscillators>();

    oscillators->section[0].prepare (sr, 1, noiseSeed);
    oscillators->section[1].prepare (sr, 2, noiseSeed ^ 0x5bd1e995u);
    oversampleStep = (float) (1.0 / std::max (1.0, OscSection::switchSeconds * sr));
    ampMixStep = (float) (1.0 / std::max (1.0, ampSwitchSeconds * sr));
    filterWetStep = (float) (1.0 / std::max (1.0, filterBypassSeconds * sr));
    velocityStep = (float) (1.0 / std::max (1.0, velocitySlewSeconds * sr));
    stealStep = (float) (1.0 / std::max (1.0, dsp::Gate::stealSeconds * sr));

    kill();
}

void Voice::kill() noexcept
{
    silence();

    if (oscillators != nullptr)
    {
        for (auto& s : oscillators->section)
            s.clear();
    }

    sectionLive[0] = sectionLive[1] = false;
    oversampleMix = 0.0f;
    previewOsc.reset (0.0);
    previewRunning = false;
    stereoFilter = false;
    warmSamples = 0;

    filter.reset();
    filterR.reset();
    ampCache = {};
    filterCache = {};
    glideRemaining = 0;
    hasPlayed = false;
    state = State::idle;
}

/** Back to "free": no note, no pending note, envelopes and gate at rest. The
    oscillator phases and the last pitch stay, for phase-continuous and gliding
    notes. */
void Voice::silence() noexcept
{
    hasPending = false;
    sustainPending = false;
    needsInit = false;
    gate.reset();
    ampEnv.reset();
    filterEnv.reset();
    filterEnvLevel = 0.0f;
    filterLive = false;
    stealGain = 1.0f;
    ampHold = 0;
}

void Voice::start (const Pending& n) noexcept
{
    note = n.note;
    velocity = n.velocity;
    preview = n.preview;
    previewWave = n.wave;
    sustainPending = n.keyReleased;
    hasPending = false;
    stealGain = 1.0f;
    filterEnvLevel = 0.0f;

    if (! preview)
        glideTo ((double) n.note);

    ampEnv.reset();
    filterEnv.reset();
    ampEnv.gateOn();
    filterEnv.gateOn();
    gate.reset();
    gate.open();

    needsInit = true; // everything that depends on the patch happens in the next render
    state = State::held;
}

void Voice::retrigger (const Pending& n, bool restartEnvelopes) noexcept
{
    note = n.note;
    sustainPending = false;
    glideTo ((double) n.note);

    if (restartEnvelopes)
    {
        velocity = n.velocity;
        ampEnv.gateOn();
        filterEnv.gateOn();
        gate.open();
        restartLfos = true;
    }

    state = State::held;
}

void Voice::steal (const Pending& p) noexcept
{
    pending = p;
    hasPending = true;

    if (state != State::stealing)
    {
        stealGain = 1.0f;
        state = State::stealing;
    }
}

void Voice::release (std::uint64_t order) noexcept
{
    sustainPending = false;
    releaseOrder = order;
    state = State::releasing;
    gate.close();
    ampEnv.gateOff();
    filterEnv.gateOff();
}

void Voice::finishSteal() noexcept
{
    if (hasPending)
    {
        const Pending p = pending;
        start (p);
    }
    else
    {
        silence();
        state = State::idle;
    }
}

void Voice::init (const EngineParams& p) noexcept
{
    needsInit = false;

    // The oscillators run `latencySamples` late. Holding the amp stage for as
    // long keeps the first sound from arriving with the attack already over.
    ampHold = dsp::Oscillator::latencySamples;

    // The oscillators restart, with or without a phase reset, when their block runs.
    for (auto& s : oscillators->section)
        s.stop();

    sectionLive[0] = sectionLive[1] = false;
    oversampleMix = 0.0f;
    previewRunning = false;
    stereoFilter = false;
    warmSamples = 0;

    modPrimed = false;
    modEnd = {};
    sources = {};
    restartLfos = true; // the LFOs that retrigger start their cycle (and fade-in) with the note

    ampMix = p.ampEnv.enabled ? 1.0f : 0.0f;
    filterWet = p.filter.enabled ? 1.0f : 0.0f;
    velocityNow = velocityGain (p);

    filter.setMode (p.filter.mode);
    filter.reset();
    filterR.setMode (p.filter.mode);
    filterR.reset();
    filterLive = false;
}

void Voice::render (float* left, float* right, int numSamples, const EngineParams& p, const BlockLevels& lv, double bend, const Taps& taps) noexcept
{
    if (numSamples <= 0)
        return;

    // A finished steal fade hands the voice to the pending note at a block
    // boundary; the voice is silent in between, so nothing audible moves.
    if (state == State::stealing && stealGain <= 0.0f)
        finishSteal();

    if (state == State::idle)
        return;

    if (needsInit)
        init (p);

    if (preview)
        renderPreview (left, right, numSamples, lv, bend);
    else
        renderPlayed (left, right, numSamples, p, lv, bend, taps);
}

/** Reads every modulation source as it stands at the end of this block and adds
    up the connections. The LFOs that retrigger run here, per voice; the others
    arrive in `lv`, one value shared by every voice. */
void Voice::computeModulation (const EngineParams& p, const BlockLevels& lv, int n, float filterEnvEnd) noexcept
{
    if (restartLfos)
    {
        for (auto& l : lfos)
            l.reset (0.0);

        age = 0.0;
        restartLfos = false;
    }

    age += (double) n / sampleRate;
    sources = {};

    for (size_t i = 0; i < lfos.size(); ++i)
    {
        const auto& lp = p.lfo[i];

        if (! lp.enabled)
            continue;

        float value;

        if (lp.retrigger)
        {
            auto& l = lfos[i];
            l.setShape (lp.shape);
            l.setRate (modulatedLfoRate (lp.rateHz, modEnd[index (lfoRateDest (i))])); // last block's rate modulation
            value = l.advance (n);
        }
        else
        {
            value = lv.lfo[i];
        }

        const float fade = lp.fadeSeconds > 0.0f ? (float) std::min (1.0, age / (double) lp.fadeSeconds) : 1.0f;
        sources[index (lfoSource (i))] = value * fade;
    }

    sources[index (ModSource::ampEnv)] = p.ampEnv.enabled ? ampEnv.getLevel() : 0.0f;
    sources[index (ModSource::filterEnv)] = p.filterEnv.enabled ? filterEnvEnd : 0.0f;
    sources[index (ModSource::velocity)] = velocity;
    sources[index (ModSource::modWheel)] = lv.modWheel;
    sources[index (ModSource::aftertouch)] = lv.aftertouch;
    sources[index (ModSource::key)] = std::clamp ((float) (note - 60) / 60.0f, -1.0f, 1.0f);

    modEnd = accumulate (p.mod, sources);
}

void Voice::renderPlayed (float* left, float* right, int n, const EngineParams& p, const BlockLevels& lv, double bend, const Taps& taps) noexcept
{
    const float invN = 1.0f / (float) n;

    const double pitchStart = glidePitch;
    const double pitchEnd = advanceGlide (n);

    syncEnvelope (ampEnv, ampCache, p.ampEnv);
    syncEnvelope (filterEnv, filterCache, p.filterEnv);

    // The filter envelope is read once per block (start and end) and the
    // cutoff is interpolated between them.
    const float envStart = filterEnvLevel;
    for (int i = 0; i < n; ++i)
        filterEnv.next();
    const float envEnd = filterEnv.getLevel();
    filterEnvLevel = envEnd;

    // Modulation is evaluated once per block like the envelope; every
    // destination moves in a straight line from last block's sum to this one's.
    ModSums modStart = modEnd;
    computeModulation (p, lv, n, envEnd);

    if (! modPrimed)
    {
        modStart = modEnd;
        modPrimed = true;
    }

    const auto rampOf = [&] (ModDest d) noexcept { return Ramp { modStart[index (d)], modEnd[index (d)] }; };
    const auto levelRamp = [&] (const Ramp& sums, float maxFactor) noexcept
    {
        return Ramp { levelFactor (sums.start, maxFactor), levelFactor (sums.end, maxFactor) };
    };

    const Ramp cutoffMod = rampOf (ModDest::cutoff);
    const Ramp resonanceMod = rampOf (ModDest::resonance);

    const float tracking = p.filter.keyTracking / 12.0f;
    const Ramp cutoff {
        lv.cutoffLog2.start + tracking * (float) (pitchStart - 60.0) + lv.filterEnvAmount.start * filterEnvOctaves * envStart
            + modCutoffOctaves * cutoffMod.start,
        lv.cutoffLog2.end + tracking * (float) (pitchEnd - 60.0) + lv.filterEnvAmount.end * filterEnvOctaves * envEnd
            + modCutoffOctaves * cutoffMod.end
    };

    auto& sections = oscillators->section;
    std::array<bool, 3> on {};
    std::array<bool, 3> synced {};
    std::array<BankSettings, 3> banks {};
    std::array<Ramp, 3> oscFactor {};

    const auto present = [] (const Ramp& r) noexcept { return ! (dsp::same (r.start, 0.0f) && dsp::same (r.end, 0.0f)); };

    // An oscillator runs while it is audible, or while ring modulation, FM or sync needs it
    // (they work on oscillators whose own level is -inf).
    for (size_t k = 0; k < on.size(); ++k)
        on[k] = present (lv.osc[k]);

    const bool ringOn = present (lv.ring);
    if (ringOn)
        on[0] = on[1] = true;

    const std::array<bool, 2> fmOn { present (lv.fm[0]) && on[1], present (lv.fm[1]) && on[2] };
    const bool fmWanted = fmOn[0] || fmOn[1];
    if (fmWanted)
        on[0] = true;

    for (size_t k = 1; k < synced.size(); ++k)
    {
        synced[k] = p.osc[k].sync && on[k] && p.osc[0].enabled;
        if (synced[k])
            on[0] = true;
    }

    for (size_t k = 0; k < oscFactor.size(); ++k)
    {
        if (! on[k])
            continue;

        const auto& op = p.osc[k];
        const double pitchMod = (double) modPitchSemitones * (double) (modEnd[index (ModDest::pitchAll)] + modEnd[index (pitchDest (k))]);

        auto& s = banks[k];
        s.on = true;
        s.wave = op.wave;
        s.increment = incrementFor (op, pitchEnd, bend + pitchMod);
        s.pulseWidth = std::clamp (op.pulseWidth + modPulseWidth * modEnd[index (pulseWidthDest (k))], 0.05f, 0.95f);
        s.unison = op.unison;
        s.detuneCents = op.unisonDetuneCents;
        s.spread = op.unisonSpread;
        s.antiAlias = op.antiAlias;
        s.phaseReset = op.phaseReset;
        s.sync = synced[k];
        oscFactor[k] = levelRamp (rampOf (levelDest (k)), modOscLevelMaxFactor);
    }

    // Two sections run the same oscillators, at the base rate and at twice it (for FM). Normally
    // one runs. While FM comes or goes the other starts from the first one's state and they are
    // crossfaded over 10 ms. A section that is live is ticked every sample, heard or not.
    const float mixTarget = fmWanted ? 1.0f : 0.0f;

    if (! sectionLive[0] && ! sectionLive[1])
        oversampleMix = mixTarget;

    if (fmWanted && ! sectionLive[1])
    {
        if (sectionLive[0])
        {
            sections[1].adoptFrom (sections[0]);
            warmSamples = overWarmSamples; // its decimator needs filling before it is heard
        }

        sectionLive[1] = true;
    }

    if (! fmWanted && ! sectionLive[0])
    {
        if (sectionLive[1])
            sections[0].adoptFrom (sections[1]);

        sectionLive[0] = true;
    }

    for (size_t s = 0; s < sections.size(); ++s)
        if (sectionLive[s])
            sections[s].beginBlock (banks, n);

    // The filter is stereo only while the oscillators are: a stereo filter costs twice as much.
    const bool stereo = (sectionLive[0] && sections[0].needsStereo()) || (sectionLive[1] && sections[1].needsStereo());

    if (stereo && ! stereoFilter)
        filterR = filter; // the right channel carries on from where the (mono) filter is

    stereoFilter = stereo;

    const Ramp noiseFactor = levelRamp (rampOf (ModDest::noiseLevel), modOscLevelMaxFactor);
    const Ramp ampFactor = levelRamp (rampOf (ModDest::ampLevel), modAmpLevelMaxFactor);
    const Ramp pan = rampOf (ModDest::pan);

    noise.setPink (p.noise.pink);
    filter.setMode (p.filter.mode);
    filterR.setMode (p.filter.mode);

    const float wetTarget = p.filter.enabled ? 1.0f : 0.0f;
    const float mixTargetAmp = p.ampEnv.enabled ? 1.0f : 0.0f;
    const float velocityTarget = velocityGain (p);
    bool filterParamsSet = false;

    for (int i = 0; i < n; ++i)
    {
        const float t = (float) (i + 1) * invN;

        const float mixGoal = warmSamples > 0 ? 0.0f : mixTarget;
        if (warmSamples > 0)
            --warmSamples;

        oversampleMix += std::clamp (mixGoal - oversampleMix, -oversampleStep, oversampleStep);

        const float fmTo2 = fmOn[0] ? lv.fm[0].at (t) : 0.0f;
        const float fmTo3 = fmOn[1] ? lv.fm[1].at (t) : 0.0f;
        StereoOscs y, over;

        if (sectionLive[0])
            sections[0].tick (fmTo2, fmTo3, y);

        if (sectionLive[1])
            sections[1].tick (fmTo2, fmTo3, sectionLive[0] ? over : y);

        if (sectionLive[0] && sectionLive[1])
        {
            for (size_t k = 0; k < y.size(); ++k)
            {
                y[k].l += (over[k].l - y[k].l) * oversampleMix;
                y[k].r += (over[k].r - y[k].r) * oversampleMix;
            }
        }

        float sumL = 0.0f, sumR = 0.0f;

        for (size_t k = 0; k < y.size(); ++k)
        {
            if (! on[k])
                continue;

            const float g1 = lv.osc[k].at (t);
            const float g2 = oscFactor[k].at (t);
            sumL += y[k].l * g1 * g2;

            if (stereo)
                sumR += y[k].r * g1 * g2;
        }

        if (ringOn)
        {
            const float ringGain = lv.ring.at (t);
            sumL += y[0].l * y[1].l * ringGain;

            if (stereo)
                sumR += y[0].r * y[1].r * ringGain;
        }

        const float noiseGain = lv.noise.at (t);
        if (noiseGain > 0.0f)
        {
            const float hiss = noise.next() * noiseGain * noiseFactor.at (t);
            sumL += hiss;
            sumR += hiss;
        }

        if (! stereo)
            sumR = sumL;

        if (taps.afterOscillators != nullptr)
            taps.afterOscillators[i] += stereo ? 0.5f * (sumL + sumR) : sumL;

        if (filterWet < wetTarget)
            filterWet = std::min (wetTarget, filterWet + filterWetStep);
        else if (filterWet > wetTarget)
            filterWet = std::max (wetTarget, filterWet - filterWetStep);

        if (filterWet > 0.0f)
        {
            // The coefficients need trig; every 8th sample is plenty for a
            // cutoff that only moves smoothly.
            if (! filterParamsSet || (i & 7) == 0)
            {
                const float resonance = std::clamp (lv.resonance.at (t) + modResonance * resonanceMod.at (t), 0.0f, 1.0f);
                const float hz = std::exp2 (cutoff.at (t));
                filter.setParams (hz, resonance, lv.driveDb.at (t));

                if (stereo)
                    filterR.setParams (hz, resonance, lv.driveDb.at (t));

                filterParamsSet = true;
            }

            const float filteredL = filter.process (sumL);
            sumL += (filteredL - sumL) * filterWet;

            if (stereo)
            {
                const float filteredR = filterR.process (sumR);
                sumR += (filteredR - sumR) * filterWet;
            }
            else
            {
                sumR = sumL;
            }

            filterLive = true;
        }
        else if (filterLive)
        {
            filter.reset(); // so it starts clean the next time it is switched in
            filterR.reset();
            filterLive = false;
        }

        if (taps.afterFilter != nullptr)
            taps.afterFilter[i] += stereo ? 0.5f * (sumL + sumR) : sumL;

        velocityNow += std::clamp (velocityTarget - velocityNow, -velocityStep, velocityStep);

        float amp = 0.0f;
        if (ampHold > 0)
        {
            --ampHold;
        }
        else
        {
            if (ampMix < mixTargetAmp)
                ampMix = std::min (mixTargetAmp, ampMix + ampMixStep);
            else if (ampMix > mixTargetAmp)
                ampMix = std::max (mixTargetAmp, ampMix - ampMixStep);

            const float gateLevel = gate.next();
            const float envLevel = ampEnv.next() * velocityNow;
            amp = gateLevel + (envLevel - gateLevel) * ampMix;
        }

        applyStealFade (amp);

        // Balance pan: the far side is turned down, the near side is left alone,
        // so a centred voice is exactly as loud as before there was a pan.
        const float panNow = std::clamp (pan.at (t), -1.0f, 1.0f) * modPan;
        const float ampNow = ampFactor.at (t);
        const float outL = sumL * amp * ampNow;
        const float outR = stereo ? sumR * amp * ampNow : outL;
        left[i] += outL * (panNow > 0.0f ? 1.0f - panNow : 1.0f);
        right[i] += outR * (panNow < 0.0f ? 1.0f + panNow : 1.0f);

        if (ended())
            return;
    }

    // The section the crossfade has left behind stops being carried along.
    if (fmWanted && oversampleMix >= 1.0f)
        sectionLive[0] = false;
    else if (! fmWanted && oversampleMix <= 0.0f)
        sectionLive[1] = false;
}

void Voice::renderPreview (float* left, float* right, int n, const BlockLevels& lv, double bend) noexcept
{
    const float invN = 1.0f / (float) n;
    const double target = incrementFor (OscParams {}, (double) note, bend);

    if (! previewRunning)
    {
        previewOsc.reset (0.0);
        previewOsc.setPulseWidth (0.5);
        previewIncrement = target;
        previewRunning = true;
    }

    const double step = (target - previewIncrement) / (double) n;

    for (int i = 0; i < n; ++i)
    {
        previewIncrement += step;
        previewOsc.setIncrement (previewIncrement);
        const float s = previewOsc.process (previewWave);

        float amp = 0.0f;
        if (ampHold > 0)
            --ampHold;
        else
            amp = gate.next();

        applyStealFade (amp);
        const float out = s * lv.preview.at ((float) (i + 1) * invN) * amp;
        left[i] += out;
        right[i] += out;

        if (ended())
            return;
    }
}

void Voice::applyStealFade (float& amp) noexcept
{
    if (state != State::stealing)
        return;

    stealGain = std::max (0.0f, stealGain - stealStep);
    amp *= stealGain;
}

bool Voice::ended() noexcept
{
    if (state == State::stealing)
        return stealGain <= 0.0f;

    if (state == State::releasing && ampSilent())
    {
        silence();
        state = State::idle;
        return true;
    }

    return false;
}

bool Voice::ampSilent() const noexcept
{
    if (preview)
        return gate.isClosedAndSilent();

    // Only the stage that is audible has to be done; a long envelope release
    // must not keep a voice alive after the gate has taken over.
    const bool envelopeDone = ampMix <= 0.0f || ampEnv.isIdle();
    const bool gateDone = ampMix >= 1.0f || gate.isClosedAndSilent();
    return envelopeDone && gateDone;
}

void Voice::glideTo (double target) noexcept
{
    if (! hasPlayed || glideSeconds <= 0.0)
    {
        glidePitch = target;
        glideRemaining = 0;
    }
    else
    {
        glideRemaining = std::max (1, (int) std::lround (glideSeconds * sampleRate));
        glideTarget = target;
        glideStep = (target - glidePitch) / (double) glideRemaining;
    }

    hasPlayed = true;
}

double Voice::advanceGlide (int numSamples) noexcept
{
    if (glideRemaining > 0)
    {
        const int steps = std::min (numSamples, glideRemaining);
        glidePitch += glideStep * (double) steps;
        glideRemaining -= steps;

        if (glideRemaining == 0)
            glidePitch = glideTarget;
    }

    return glidePitch;
}

double Voice::incrementFor (const OscParams& o, double pitch, double bend) const noexcept
{
    const double semitones = pitch + 12.0 * (double) o.octave + (double) o.semitone + (double) o.fineCents / 100.0 + bend;
    const double hz = 440.0 * std::exp2 ((semitones - 69.0) / 12.0);

    // Keeps the extreme corner (top note, +2 octaves, full bend) below Nyquist.
    return std::min (hz / sampleRate, 0.45);
}

float Voice::velocityGain (const EngineParams& p) const noexcept
{
    const float s = std::clamp (p.ampEnv.velocitySensitivity, 0.0f, 1.0f);
    return (1.0f - s) + s * velocity;
}

void Voice::syncEnvelope (dsp::Envelope& env, EnvelopeCache& cache, const EnvelopeParams& e) noexcept
{
    if (dsp::same (cache.attack, e.attack) && dsp::same (cache.decay, e.decay)
        && dsp::same (cache.sustain, e.sustain) && dsp::same (cache.release, e.release))
        return;

    cache = { e.attack, e.decay, e.sustain, e.release };
    env.setParams (e.attack, e.decay, e.sustain, e.release);
}

} // namespace scratchpad::engine
