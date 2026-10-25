#pragma once

#include "dsp/Chorus.h"
#include "dsp/Delay.h"
#include "dsp/Distortion.h"
#include "dsp/Reverb.h"
#include "engine/EngineParams.h"

#include <array>

namespace scratchpad::engine
{

/** The global effects: distortion, chorus, delay and reverb on the summed stereo mix, in
    `EngineParams::effectOrder`, only the ones that are `enabled`.

    Nothing steps. Each effect smooths its own parameters. An effect that is switched on or off
    is crossfaded with the signal arriving at it over 15 ms (the effect keeps running, tail and
    all, until the fade has finished); a finished-off effect is skipped entirely and costs
    nothing, and its memory is cleared when it is next switched on. Changing the order of
    effects that are audible fades the whole chain's output to silence over 10 ms, swaps the
    order while it is silent, and fades back in over 10 ms.

    Distortion's oversampling delays its wet path, so inside it the dry path is delayed to
    match. `getLatencySamples()` is that delay, a constant for a sample rate; the audio is
    late by it only while distortion is on, but hosts are told about it always, so the
    reported latency never jumps. */
class EffectsChain
{
public:
    void prepare (double sampleRate);

    /** The audio thread's copy of the effect settings; takes effect at the next `process`. */
    void setParams (const EngineParams& params) noexcept;

    /** In place, any number of samples. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Valid after `prepare`. */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** True when nothing is running (every effect off and faded out, no reorder under way). */
    bool isIdle() const noexcept;

private:
    static constexpr int chunkSize = 32;

    struct Slot
    {
        bool wanted = false; // the effect's `enabled`
        bool active = false; // running: wanted, or fading out
        float fade = 0.0f;   // 0: the signal arriving, 1: the effect's output
    };

    enum class Duck { idle, fadingOut, silent, fadingIn };

    static size_t slotOf (EffectKind kind) noexcept { return (size_t) kind; }

    void applyParams (bool atOnce) noexcept;
    void setEffectParams (EffectKind kind, bool jump) noexcept;
    void activate (EffectKind kind, bool atOnce) noexcept;
    void runEffect (EffectKind kind, float* left, float* right, int n) noexcept;
    void processChunk (float* left, float* right, int n) noexcept;
    void checkOrder() noexcept;
    bool audibleOrderChange() const noexcept;
    void applyDuck (float* left, float* right, int n) noexcept;

    dsp::Distortion distortion;
    dsp::Chorus chorus;
    dsp::Delay delay;
    dsp::Reverb reverb;

    DistortionParams distortionParams;
    ChorusParams chorusParams;
    DelayParams delayParams;
    ReverbParams reverbParams;

    std::array<Slot, (size_t) numEffects> slots;
    std::array<EffectKind, (size_t) numEffects> order { EffectKind::distortion, EffectKind::chorus, EffectKind::delay, EffectKind::reverb };
    std::array<EffectKind, (size_t) numEffects> requestedOrder = order;

    Duck duckState = Duck::idle;
    float duck = 1.0f;

    float fadeStep = 0.001f;
    float duckStep = 0.001f;
    int latencySamples = 0;
    bool prepared = false;

    std::array<float, chunkSize> inLeft {}, inRight {};
};

} // namespace scratchpad::engine
