#include "engine/EffectsChain.h"

#include <algorithm>

namespace scratchpad::engine
{

namespace
{
    constexpr double fadeSeconds = 0.015;
    constexpr double duckSeconds = 0.010;
}

void EffectsChain::prepare (double sampleRate)
{
    distortion.prepare (sampleRate);
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);

    latencySamples = distortion.getLatencySamples();
    fadeStep = (float) (1.0 / (fadeSeconds * sampleRate));
    duckStep = (float) (1.0 / (duckSeconds * sampleRate));

    slots = {};
    order = requestedOrder;
    duckState = Duck::idle;
    duck = 1.0f;
    prepared = true;

    // A patch that arrives with effects on has them on from the first sample.
    applyParams (true);
}

void EffectsChain::setParams (const EngineParams& p) noexcept
{
    distortionParams = p.distortion;
    chorusParams = p.chorus;
    delayParams = p.delay;
    reverbParams = p.reverb;
    requestedOrder = p.effectOrder;

    if (prepared)
        applyParams (false);
}

bool EffectsChain::isIdle() const noexcept
{
    for (const auto& slot : slots)
        if (slot.active)
            return false;

    return duckState == Duck::idle && order == requestedOrder;
}

void EffectsChain::applyParams (bool atOnce) noexcept
{
    const std::array<bool, (size_t) numEffects> wanted {
        distortionParams.enabled, chorusParams.enabled, delayParams.enabled, reverbParams.enabled
    };

    for (int i = 0; i < numEffects; ++i)
    {
        const auto kind = (EffectKind) i;
        auto& slot = slots[(size_t) i];

        slot.wanted = wanted[(size_t) i];

        if (! slot.wanted)
            continue;

        if (! slot.active)
            activate (kind, atOnce);
        else
            setEffectParams (kind, false);
    }
}

void EffectsChain::activate (EffectKind kind, bool atOnce) noexcept
{
    switch (kind)
    {
        case EffectKind::distortion: distortion.reset(); break;
        case EffectKind::chorus:     chorus.reset();     break;
        case EffectKind::delay:      delay.reset();      break;
        case EffectKind::reverb:     reverb.reset();     break;
    }

    setEffectParams (kind, true);

    auto& slot = slots[slotOf (kind)];
    slot.active = true;
    slot.fade = atOnce ? 1.0f : 0.0f;
}

void EffectsChain::setEffectParams (EffectKind kind, bool jump) noexcept
{
    switch (kind)
    {
        case EffectKind::distortion:
            distortion.setParams (distortionParams.driveDb, distortionParams.tone, distortionParams.mix, jump);
            break;
        case EffectKind::chorus:
            chorus.setParams (chorusParams.rateHz, chorusParams.depth, chorusParams.mix, jump);
            break;
        case EffectKind::delay:
            delay.setParams (delayParams.timeMs, delayParams.feedback, delayParams.pingPong, delayParams.mix, jump);
            break;
        case EffectKind::reverb:
            reverb.setParams (reverbParams.size, reverbParams.decaySeconds, reverbParams.damping, reverbParams.mix, jump);
            break;
    }
}

void EffectsChain::runEffect (EffectKind kind, float* left, float* right, int n) noexcept
{
    switch (kind)
    {
        case EffectKind::distortion: distortion.process (left, right, n); break;
        case EffectKind::chorus:     chorus.process (left, right, n);     break;
        case EffectKind::delay:      delay.process (left, right, n);      break;
        case EffectKind::reverb:     reverb.process (left, right, n);     break;
    }
}

void EffectsChain::process (float* left, float* right, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    int done = 0;

    while (done < numSamples)
    {
        const int n = std::min (chunkSize, numSamples - done);
        processChunk (left + done, right + done, n);
        done += n;
    }
}

bool EffectsChain::audibleOrderChange() const noexcept
{
    // Only effects that are running matter: reordering the others changes nothing you can hear.
    std::array<EffectKind, (size_t) numEffects> before {}, after {};
    size_t nBefore = 0, nAfter = 0;

    for (size_t k = 0; k < (size_t) numEffects; ++k)
    {
        if (slots[slotOf (order[k])].active)
            before[nBefore++] = order[k];

        if (slots[slotOf (requestedOrder[k])].active)
            after[nAfter++] = requestedOrder[k];
    }

    return ! std::equal (before.begin(), before.begin() + (std::ptrdiff_t) nBefore,
                         after.begin(), after.begin() + (std::ptrdiff_t) nAfter);
}

void EffectsChain::checkOrder() noexcept
{
    if (order == requestedOrder || duckState == Duck::fadingOut || duckState == Duck::silent)
        return;

    if (audibleOrderChange())
        duckState = Duck::fadingOut;
    else
        order = requestedOrder;
}

void EffectsChain::processChunk (float* left, float* right, int n) noexcept
{
    checkOrder();

    for (size_t k = 0; k < (size_t) numEffects; ++k)
    {
        const auto kind = order[k];
        auto& slot = slots[slotOf (kind)];

        if (! slot.active)
            continue;

        const float target = slot.wanted ? 1.0f : 0.0f;

        if (slot.fade == 1.0f && target == 1.0f)
        {
            runEffect (kind, left, right, n);
            continue;
        }

        std::copy_n (left, n, inLeft.begin());
        std::copy_n (right, n, inRight.begin());

        runEffect (kind, left, right, n);

        float fade = slot.fade;

        for (int i = 0; i < n; ++i)
        {
            fade += std::clamp (target - fade, -fadeStep, fadeStep);
            left[i] = inLeft[(size_t) i] + (left[i] - inLeft[(size_t) i]) * fade;
            right[i] = inRight[(size_t) i] + (right[i] - inRight[(size_t) i]) * fade;
        }

        slot.fade = fade;

        if (fade == 0.0f && ! slot.wanted)
            slot.active = false;
    }

    applyDuck (left, right, n);
}

void EffectsChain::applyDuck (float* left, float* right, int n) noexcept
{
    if (duckState == Duck::idle)
        return;

    for (int i = 0; i < n; ++i)
    {
        if (duckState == Duck::fadingOut)
        {
            duck -= duckStep;

            if (duck <= 0.0f)
            {
                duck = 0.0f;
                duckState = Duck::silent; // held at zero until the order can be swapped, at the chunk's end
            }
        }
        else if (duckState == Duck::fadingIn)
        {
            duck += duckStep;

            if (duck >= 1.0f)
            {
                duck = 1.0f;
                duckState = Duck::idle;
            }
        }

        left[i] *= duck;
        right[i] *= duck;
    }

    if (duckState == Duck::silent)
    {
        order = requestedOrder;
        duckState = Duck::fadingIn;
    }
}

} // namespace scratchpad::engine
