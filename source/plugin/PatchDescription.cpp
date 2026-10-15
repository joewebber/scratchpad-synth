#include "plugin/PatchDescription.h"

#include "plugin/Parameters.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace scratchpad::plugin
{

using engine::ModDest;
using engine::ModSource;

namespace
{
    juce::String waveName (dsp::Wave w, bool plural)
    {
        static const char* singular[] = { "sine", "triangle", "saw", "pulse" };
        static const char* many[] = { "sines", "triangles", "saws", "pulses" };
        return (plural ? many : singular)[(int) w];
    }

    juce::String countWord (int n)
    {
        static const char* words[] = { "No", "One", "Two", "Three" };
        return words[juce::jlimit (0, 3, n)];
    }

    juce::String withArticle (const juce::String& noun)
    {
        return (juce::String ("aeiou").containsChar (noun[0]) ? "an " : "a ") + noun;
    }

    /** Total pitch offset of an oscillator from the note, in cents. */
    float offsetCents (const engine::OscParams& o)
    {
        return 1200.0f * (float) o.octave + 100.0f * (float) o.semitone + o.fineCents;
    }

    juce::String sources (const engine::EngineParams& p)
    {
        std::vector<const engine::OscParams*> on;
        for (const auto& o : p.osc)
            if (o.enabled && o.levelDb > engine::minLevelDb)
                on.push_back (&o);

        if (on.empty())
            return "Silence";

        const bool sameWave = std::all_of (on.begin(), on.end(), [&] (auto* o) { return o->wave == on.front()->wave; });
        juce::String text;

        if (on.size() == 1)
        {
            text = "One " + waveName (on[0]->wave, false) + " wave";
        }
        else if (sameWave)
        {
            text = countWord ((int) on.size()) + " " + waveName (on[0]->wave, true);
        }
        else
        {
            text = withArticle (waveName (on[0]->wave, false)).replaceSection (0, 1, "A");
            for (size_t i = 1; i < on.size(); ++i)
                text += (i + 1 == on.size() ? " and " : ", ") + withArticle (waveName (on[i]->wave, false));
        }

        if (on.size() > 1)
        {
            // How far the others sit from the first.
            std::vector<float> apart;
            for (size_t i = 1; i < on.size(); ++i)
                apart.push_back (offsetCents (*on[i]) - offsetCents (*on[0]));

            const float first = apart.front();
            const bool allSame = std::all_of (apart.begin(), apart.end(), [&] (float a) { return std::abs (a - first) < 0.5f; });

            if (allSame && std::abs (first) >= 0.5f)
            {
                const float magnitude = std::abs (first);
                const int octaves = (int) std::round (magnitude / 1200.0f);

                if (magnitude < 100.0f)
                    text += " detuned by " + juce::String (juce::roundToInt (magnitude)) + (juce::roundToInt (magnitude) == 1 ? " cent" : " cents");
                else if (octaves >= 1 && std::abs (magnitude - 1200.0f * (float) octaves) < 0.5f)
                    text += juce::String (octaves == 1 ? ", an octave " : ", " + juce::String (octaves) + " octaves ") + (first < 0 ? "down" : "up");
                else
                    text += ", " + juce::String (juce::roundToInt (magnitude / 100.0f)) + " semitones " + (first < 0 ? "down" : "up");
            }
            else if (! allSame)
            {
                text += " tuned apart";
            }
        }

        if (p.noise.enabled && p.noise.levelDb > engine::minLevelDb)
            text += juce::String (", with ") + (p.noise.levelDb > -12.0f ? "plenty of " : "a little ")
                  + (p.noise.pink ? "pink" : "white") + " noise";

        return text;
    }

    juce::String filterText (const engine::FilterParams& f)
    {
        static const char* names[] = { "24 dB low-pass", "12 dB low-pass", "12 dB high-pass", "12 dB band-pass" };
        auto text = " through a " + juce::String (names[(int) f.mode]) + " filter at " + params::frequencyText (f.cutoffHz);

        if (f.resonance >= 0.9f)
            text += ", ringing on the edge of self-oscillation";
        else if (f.resonance >= 0.6f)
            text += " with strong resonance";

        return text;
    }

    juce::String speed (float seconds)
    {
        return seconds < 0.02f ? "fast" : seconds < 0.3f ? "quick" : seconds < 1.5f ? "slow" : "very slow";
    }

    /** The strongest connection from `source` to `dest`, or 0. */
    float amountOf (const engine::EngineParams& p, ModSource source, ModDest dest)
    {
        float best = 0.0f;
        for (const auto& c : p.mod)
            if (c.active() && c.source == source && c.dest == dest && std::abs (c.amount) > std::abs (best))
                best = c.amount;
        return best;
    }

    bool lfoPresent (const engine::EngineParams& p, ModSource s)
    {
        return p.lfo[s == ModSource::lfo1 ? 0 : 1].enabled;
    }

    /** True if any connection has this source on this destination. */
    bool connected (const engine::EngineParams& p, ModSource s, std::initializer_list<ModDest> dests)
    {
        for (const auto& c : p.mod)
            if (c.active() && c.source == s && std::find (dests.begin(), dests.end(), c.dest) != dests.end())
                return true;
        return false;
    }
}

juce::String describe (const engine::EngineParams& p)
{
    const auto base = sources (p);

    if (base == "Silence")
        return "Nothing is making sound yet.";

    auto text = base;

    if (p.filter.enabled)
        text += filterText (p.filter);

    if (p.filter.enabled && p.filterEnv.enabled && std::abs (p.filterEnv.amount) >= 0.05f)
    {
        const float time = p.filterEnv.amount > 0 ? p.filterEnv.decay : p.filterEnv.decay;
        text += juce::String (p.filterEnv.amount > 0 ? ", opened" : ", closed") + " by " + withArticle (speed (std::max (p.filterEnv.attack, time)))
              + " envelope";
    }

    if (p.ampEnv.enabled)
    {
        const bool snappy = p.ampEnv.attack < 0.02f && p.ampEnv.release < 0.3f;
        text += juce::String (p.filter.enabled ? ", with " : ", shaped by ") + withArticle (snappy ? "snappy" : p.ampEnv.attack >= 0.3f ? "slow-fading" : "smooth")
              + " amp envelope";
    }
    else if (! p.filter.enabled)
    {
        text += ", switched straight on and off by the keys";
    }

    // Movement.
    std::vector<juce::String> motion;

    for (const auto lfo : { ModSource::lfo1, ModSource::lfo2 })
    {
        if (! lfoPresent (p, lfo))
            continue;

        const auto name = lfo == ModSource::lfo1 ? "LFO 1" : "LFO 2";
        const float pitch = std::max (std::abs (amountOf (p, lfo, ModDest::pitchAll)), std::max (std::abs (amountOf (p, lfo, ModDest::pitch1)),
                            std::max (std::abs (amountOf (p, lfo, ModDest::pitch2)), std::abs (amountOf (p, lfo, ModDest::pitch3)))));

        if (pitch > 0.0f)
            motion.push_back (pitch <= 0.3f ? "a gentle vibrato" : pitch <= 0.7f ? "a clear vibrato" : "a deep vibrato");

        if (connected (p, lfo, { ModDest::cutoff }))
            motion.push_back (juce::String (p.lfo[lfo == ModSource::lfo1 ? 0 : 1].rateHz < 1.0f ? "a slowly sweeping" : "a wobbling") + " filter");

        if (connected (p, lfo, { ModDest::level1, ModDest::level2, ModDest::level3, ModDest::noiseLevel, ModDest::ampLevel }))
            motion.push_back ("a tremolo");

        if (connected (p, lfo, { ModDest::pulseWidth1, ModDest::pulseWidth2, ModDest::pulseWidth3 }))
            motion.push_back ("a moving pulse width");

        if (connected (p, lfo, { ModDest::pan }))
            motion.push_back ("a swirling stereo position");

        if (connected (p, lfo, { ModDest::lfo1Rate, ModDest::lfo2Rate }))
            motion.push_back (juce::String (name) + " changing the other LFO's speed");

        if (! connected (p, lfo, { ModDest::pitchAll, ModDest::pitch1, ModDest::pitch2, ModDest::pitch3, ModDest::cutoff, ModDest::resonance,
                                   ModDest::level1, ModDest::level2, ModDest::level3, ModDest::noiseLevel, ModDest::ampLevel,
                                   ModDest::pulseWidth1, ModDest::pulseWidth2, ModDest::pulseWidth3, ModDest::pan,
                                   ModDest::lfo1Rate, ModDest::lfo2Rate }))
            motion.push_back (juce::String (name) + " running but connected to nothing");
    }

    if (connected (p, ModSource::velocity, { ModDest::cutoff }))
        motion.push_back ("harder keys opening the filter");
    if (connected (p, ModSource::modWheel, { ModDest::cutoff, ModDest::resonance, ModDest::pitchAll }))
        motion.push_back ("the mod wheel in charge of the sound");

    for (size_t i = 0; i < motion.size(); ++i)
        text += (i == 0 ? ", with " : i + 1 == motion.size() ? " and " : ", ") + motion[i];

    return text + ".";
}

} // namespace scratchpad::plugin
