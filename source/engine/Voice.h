#pragma once

#include "dsp/Distortion.h"
#include "dsp/Envelope.h"
#include "dsp/Gate.h"
#include "dsp/Lfo.h"
#include "dsp/MultimodeFilter.h"
#include "dsp/Noise.h"
#include "dsp/Oscillator.h"
#include "engine/BlockLevels.h"
#include "engine/EngineParams.h"
#include "engine/OscSection.h"

#include <array>
#include <cstdint>
#include <memory>

namespace scratchpad::engine
{

/** One note of the per-voice chain:

        osc 1 + osc 2 + osc 3 + ring + noise -> filter -> amp (envelope, or the 2 ms gate) -> pan

    The oscillators (`OscSection`) make stereo audio: unison copies are panned, so from there on
    the voice is stereo. The filter is bypassed (10 ms crossfade) unless `filter.enabled`; it runs
    once while the signal is mono and once per channel while unison spread makes it stereo. The amp stage is the amp envelope if `ampEnv.enabled` (scaled by
    velocity), the fixed declick gate otherwise; toggling it mid-note crossfades over 10 ms.
    The filter envelope always runs; `filterEnv.enabled` only decides whether its amount is applied.

    Ring modulation (osc 1 x osc 2) joins the mix as a source at its own level. FM runs the
    oscillators in a second `OscSection` at twice the sample rate while the FM amount is above
    zero; the two sections are crossfaded over 10 ms when FM comes and goes.

    Everything is real-time safe: no allocation, locks or logging. */
class Voice
{
public:
    enum class State
    {
        idle,      // free, produces nothing
        held,      // key is down (or the sustain pedal is holding it)
        releasing, // key is up, amp stage is fading out
        stealing   // fading out over 5 ms so `pending` can take over the voice
    };

    /** A note to start: what `start`, `retrigger` and `steal` take. */
    struct Pending
    {
        int note = -1;
        float velocity = 0.0f;
        bool keyReleased = false;         // note-off arrived while the fade was running
    };

    /** Where the scope listens: each voice adds its signal for the block into these, when set. */
    struct Taps
    {
        float* afterOscillators = nullptr; // the summed sources, before the filter
        float* afterFilter = nullptr;      // after the filter, before the amp stage
    };

    static constexpr int blockSize = 32; // the longest block `render` is meant for
    static constexpr double ampSwitchSeconds = 0.010;
    static constexpr int overWarmSamples = 16; // the decimator's history, in base-rate samples
    static constexpr double filterBypassSeconds = 0.010;
    static constexpr double velocitySlewSeconds = 0.005;

    void prepare (double sr, std::uint32_t noiseSeed) noexcept;

    /** Silences everything and forgets the last pitch. */
    void kill() noexcept;

    /** Seconds a new note takes to slide from this voice's last pitch. */
    void setGlideSeconds (double seconds) noexcept { glideSeconds = seconds; }

    /** Starts `n` immediately. The voice must be idle. */
    void start (const Pending& n) noexcept;

    /** A new note on a voice that is already sounding (mono and legato):
        glides to the new pitch without touching the oscillator phase. With
        `restartEnvelopes` the envelopes attack again from where they are. */
    void retrigger (const Pending& n, bool restartEnvelopes) noexcept;

    /** Fades out over 5 ms, then starts `p`; used when every voice is busy.
        Calling it again while the fade runs only replaces the pending note. */
    void steal (const Pending& p) noexcept;

    void release (std::uint64_t order) noexcept;

    /** Adds this voice's output to `left` and `right` (pan is applied here).
        `levels` are valid for exactly this block, which is meant to be at most
        `blockSize` samples. `bendSemitones` is the pitch bend already scaled by its range. */
    void render (float* left, float* right, int numSamples, const EngineParams& params, const BlockLevels& levels, double bendSemitones, const Taps& taps) noexcept;

    void render (float* left, float* right, int numSamples, const EngineParams& params, const BlockLevels& levels, double bendSemitones) noexcept
    {
        render (left, right, numSamples, params, levels, bendSemitones, Taps {});
    }

    bool isActive() const noexcept { return state != State::idle; }

    /** The modulation sources as this voice last saw them (for the editor's live dots). */
    const ModSourceValues& getModSources() const noexcept { return sources; }

    /** The envelopes' stages, as `dsp::Envelope::Stage` values, for the editor's playheads. */
    int getAmpEnvStage() const noexcept { return (int) ampEnv.getStage(); }
    int getFilterEnvStage() const noexcept { return (int) filterEnv.getStage(); }

    /** Phase of this voice's own LFO; it only moves while that LFO retriggers. */
    double getLfoPhase (size_t lfo) const noexcept { return lfos[lfo].getPhase(); }

    State state = State::idle;
    int note = -1;
    float velocity = 0.0f;
    bool sustainPending = false; // key is up but the pedal is holding the note
    std::uint64_t startOrder = 0;
    std::uint64_t releaseOrder = 0;
    bool hasPending = false;
    Pending pending;

private:
    /** The oscillators run at the base rate, or (second) at twice that whenever FM is on. */
    struct Oscillators
    {
        std::array<OscSection, 2> section;
    };

    struct EnvelopeCache
    {
        float attack = -1.0f, decay = -1.0f, sustain = -1.0f, release = -1.0f; // -1: never set
    };

    void init (const EngineParams& p) noexcept;
    void finishSteal() noexcept;
    void silence() noexcept;
    void renderPlayed (float* left, float* right, int numSamples, const EngineParams& p, const BlockLevels& lv, double bend, const Taps& taps) noexcept;
    void computeModulation (const EngineParams& p, const BlockLevels& lv, int numSamples, float filterEnvEnd) noexcept;

    void glideTo (double target) noexcept;
    double advanceGlide (int numSamples) noexcept;
    double incrementFor (const OscParams& o, double pitch, double bend) const noexcept;
    float velocityGain (const EngineParams& p) const noexcept;
    bool ampSilent() const noexcept;
    void applyStealFade (float& amp) noexcept;
    /** True once the voice has nothing more to output this block: it went idle, or its steal fade finished. */
    bool ended() noexcept;

    static void syncEnvelope (dsp::Envelope& env, EnvelopeCache& cache, const EnvelopeParams& e) noexcept;

    std::unique_ptr<Oscillators> oscillators; // large; kept off the stack
    bool sectionLive[2] { false, false };     // 1x and 2x sections: running since the last note started
    float oversampleMix = 0.0f;               // 0: all from the base-rate section, 1: all from the 2x section
    float oversampleStep = 0.01f;
    bool stereoFilter = false;
    int warmSamples = 0;                      // samples the 2x section still runs unheard, filling its decimator
    dsp::Noise noise;
    dsp::MultimodeFilter filter, filterR;
    dsp::Distortion voiceShaper;
    dsp::Envelope ampEnv, filterEnv;
    dsp::Gate gate;
    EnvelopeCache ampCache, filterCache;

    // Glide runs in the semitone domain, from the last note this voice played.
    double glidePitch = 60.0;
    double glideTarget = 60.0;
    double glideStep = 0.0;
    double glideSeconds = 0.0;
    int glideRemaining = 0;
    bool hasPlayed = false;

    float filterEnvLevel = 0.0f; // filter envelope at the end of the last block
    float ampMix = 0.0f;         // 0 = gate shapes the note, 1 = amp envelope does
    float filterWet = 0.0f;
    float velocityNow = 1.0f;
    float stealGain = 1.0f;
    bool filterLive = false;     // the filter has been fed since it was last reset
    bool needsInit = false;
    int ampHold = 0;             // samples the amp stage still waits for the oscillators' own delay

    // Modulation. The LFOs here are the voice's own, used while an LFO retriggers.
    std::array<dsp::Lfo, 2> lfos;
    ModSourceValues sources {};  // as of the end of the last block
    ModSums modEnd {};           // connection sums at the end of the last block
    bool modPrimed = false;      // `modEnd` holds a block this voice has rendered
    bool restartLfos = false;
    double age = 0.0;            // seconds since the note started, for the LFO fade-in

    float ampMixStep = 0.01f;
    float filterWetStep = 0.01f;
    float velocityStep = 0.01f;
    float stealStep = 0.01f;
    double sampleRate = 48000.0;
};

} // namespace scratchpad::engine
