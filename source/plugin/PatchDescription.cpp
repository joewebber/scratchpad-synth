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
    // ---- Grammar helpers -------------------------------------------------------------------------------------

    juce::String capitalised (const juce::String& s)
    {
        return s.substring (0, 1).toUpperCase() + s.substring (1);
    }

    /** "a saw", "an octave", "an A wavetable": the article follows the first letter of the next word. */
    juce::String withArticle (const juce::String& phrase)
    {
        return (juce::String ("aeiou").containsChar (juce::CharacterFunctions::toLowerCase (phrase[0])) ? "an " : "a ") + phrase;
    }

    /** One to three read as words, anything larger as digits: "two saws", "7 cents". */
    juce::String countWord (int n)
    {
        static const char* words[] = { "no", "one", "two", "three" };
        return n >= 0 && n <= 3 ? juce::String (words[n]) : juce::String (n);
    }

    juce::String ordinal (int n)
    {
        static const char* words[] = { "first", "second", "third", "fourth", "fifth" };
        return words[juce::jlimit (0, 4, n - 1)];
    }

    /** "A", "A and B", "A, B and C" (or "A, B, and C"). */
    juce::String joinList (const std::vector<juce::String>& items, bool serialComma = false)
    {
        juce::String text;

        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0)
                text += i + 1 < items.size() ? ", " : serialComma ? ", and " : " and ";

            text += items[i];
        }

        return text;
    }

    /** "2 kHz", "2.5 kHz", "440 Hz": the plain way a person says a cutoff. */
    juce::String spokenFrequency (float hz)
    {
        if (hz < 1000.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";

        auto text = juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1);

        while (text.endsWith ("0"))
            text = text.dropLastCharacters (1);

        if (text.endsWith ("."))
            text = text.dropLastCharacters (1);

        return text + " kHz";
    }

    // ---- Oscillators -----------------------------------------------------------------------------------------

    juce::String waveName (dsp::Wave w, bool plural)
    {
        static const char* singular[] = { "sine", "triangle", "saw", "pulse", "A wavetable", "B wavetable", "C wavetable" };
        static const char* many[] = { "sines", "triangles", "saws", "pulses", "A wavetables", "B wavetables", "C wavetables" };
        return (plural ? many : singular)[(int) w];
    }

    /** Total pitch offset of an oscillator from the note, in cents. */
    float offsetCents (const engine::OscParams& o)
    {
        return 1200.0f * (float) o.octave + 100.0f * (float) o.semitone + o.fineCents;
    }

    /** A signed distance in whole cents as "7 cents sharp", "an octave down", "two octaves and a semitone up". */
    juce::String distancePhrase (int cents)
    {
        const bool up = cents > 0;
        const int magnitude = std::abs (cents);

        if (magnitude < 100)
            return countWord (magnitude) + (magnitude == 1 ? " cent " : " cents ") + (up ? "sharp" : "flat");

        const int octaves = magnitude / 1200, semitones = (magnitude % 1200) / 100, rest = magnitude % 100;
        std::vector<juce::String> parts;

        if (octaves > 0)
            parts.push_back (octaves == 1 ? "an octave" : countWord (octaves) + " octaves");

        if (semitones > 0)
            parts.push_back (semitones == 1 ? "a semitone" : countWord (semitones) + " semitones");

        if (rest > 0)
            parts.push_back (countWord (rest) + (rest == 1 ? " cent" : " cents"));

        return joinList (parts) + (up ? " up" : " down");
    }

    juce::String unisonWord (int n)
    {
        static const char* words[] = { "one", "two", "three", "four", "five", "six", "seven" };
        return words[juce::jlimit (0, 6, n - 1)];
    }

    /** Unison: how many copies each oscillator is played as, only when it changes the sound. */
    juce::String unisonText (const engine::EngineParams& p)
    {
        int audibleCount = 0, unisonCount = 0, copies = 0;
        bool sameCopies = true, spread = false;

        for (const auto& o : p.osc)
        {
            if (! (o.enabled && o.levelDb > engine::minLevelDb))
                continue;

            ++audibleCount;

            if (o.unison > 1)
            {
                ++unisonCount;
                spread = spread || o.unisonDetuneCents >= 1.0f;
                sameCopies = sameCopies && (copies == 0 || copies == o.unison);
                copies = o.unison;
            }
        }

        if (unisonCount == 0)
            return {};

        const auto kind = spread ? " detuned copies" : " identical copies";

        if (unisonCount == audibleCount && sameCopies)
            return juce::String (audibleCount > 1 ? ", each played as " : ", played as ") + unisonWord (copies) + kind;

        return ", some of it played as several" + juce::String (kind);
    }

    /** Noise, hard sync, ring modulation, FM and the anti-aliasing switch: the "with ..." extras, each only when it changes the sound. */
    std::vector<juce::String> extras (const engine::EngineParams& p, bool includeNoise)
    {
        const auto audible = [&] (size_t i) { return p.osc[i].enabled && p.osc[i].levelDb > engine::minLevelDb; };
        const auto oscName = [] (size_t i) { return "Osc " + juce::String ((int) i + 1); };
        std::vector<juce::String> out;

        if (includeNoise && p.noise.enabled && p.noise.levelDb > engine::minLevelDb)
            out.push_back (juce::String (p.noise.levelDb > -12.0f ? "plenty of " : "a little ") + (p.noise.pink ? "pink" : "white") + " noise");

        if (p.osc[0].enabled)
        {
            std::vector<juce::String> synced;
            for (size_t i = 1; i < p.osc.size(); ++i)
                if (audible (i) && p.osc[i].sync)
                    synced.push_back (oscName (i));

            if (! synced.empty())
                out.push_back (joinList (synced) + " hard-synced to Osc 1");
        }

        if (p.osc[0].enabled && p.osc[1].enabled && p.ringLevelDb > engine::minLevelDb)
            out.push_back ("ring modulation between Osc 1 and Osc 2");

        if (p.osc[0].enabled && p.fmAmount > 0.01f)
        {
            std::vector<juce::String> targets;

            if (p.osc[1].enabled && p.fmTarget != engine::FmTarget::osc3)
                targets.push_back (oscName (1));

            if (p.osc[2].enabled && p.fmTarget != engine::FmTarget::osc2)
                targets.push_back (oscName (2));

            if (! targets.empty())
                out.push_back (juce::String (p.fmAmount < 0.25f ? "a touch of " : p.fmAmount >= 0.75f ? "heavy " : "")
                               + "FM from Osc 1 into " + joinList (targets));
        }

        const bool antiAliasOff = std::any_of (p.osc.begin(), p.osc.end(), [] (const engine::OscParams& o)
                                               { return o.enabled && o.levelDb > engine::minLevelDb && ! o.antiAlias; });

        if (antiAliasOff)
            out.push_back ("anti-aliasing switched off");

        return out;
    }

    /** The audible oscillators as "two saws, the second 7 cents sharp, and a sine two octaves down". */
    juce::String oscillatorList (const std::vector<const engine::OscParams*>& on)
    {
        struct Group
        {
            dsp::Wave wave;
            std::vector<int> cents; // each member's whole-cent offset from the first oscillator
        };

        std::vector<Group> groups;
        const float base = offsetCents (*on.front());

        for (const auto* o : on)
        {
            auto found = std::find_if (groups.begin(), groups.end(), [&] (const Group& g) { return g.wave == o->wave; });

            if (found == groups.end())
            {
                groups.push_back ({ o->wave, {} });
                found = groups.end() - 1;
            }

            found->cents.push_back (juce::roundToInt (offsetCents (*o) - base));
        }

        std::vector<juce::String> items;
        bool anyAppositive = false;

        for (size_t g = 0; g < groups.size(); ++g)
        {
            const auto& group = groups[g];
            const int n = (int) group.cents.size();
            juce::String item;

            if (n == 1)
                item = withArticle (waveName (group.wave, false)) + (groups.size() == 1 ? " wave" : "");
            else
                item = countWord (n) + " " + waveName (group.wave, true);

            if (g == 0)
            {
                // The first oscillator is the reference; say how the others in its group sit against it.
                std::vector<int> others (group.cents.begin() + 1, group.cents.end());
                const int nonZero = (int) std::count_if (others.begin(), others.end(), [] (int c) { return c != 0; });
                const bool allSame = n == 3 && nonZero == (int) others.size()
                                  && std::all_of (others.begin(), others.end(), [&] (int c) { return c == others.front(); });

                if (nonZero > 0 && n == 2)
                {
                    item += ", the second " + distancePhrase (others.front());
                    anyAppositive = true;
                }
                else if (nonZero == 1)
                {
                    const int at = (int) (std::find_if (others.begin(), others.end(), [] (int c) { return c != 0; }) - others.begin());
                    item += ", the " + ordinal (at + 2) + " " + distancePhrase (others[(size_t) at]);
                    anyAppositive = true;
                }
                else if (allSame)
                {
                    item += ", the other two " + distancePhrase (others.front());
                    anyAppositive = true;
                }
                else if (nonZero > 0)
                {
                    item += " tuned apart";
                }
            }
            else
            {
                const bool allSame = std::all_of (group.cents.begin(), group.cents.end(), [&] (int c) { return c == group.cents.front(); });

                if (! allSame)
                    item += " tuned apart";
                else if (group.cents.front() != 0)
                    item += " " + distancePhrase (group.cents.front());
            }

            items.push_back (item);
        }

        return joinList (items, anyAppositive);
    }

    juce::String sources (const engine::EngineParams& p)
    {
        std::vector<const engine::OscParams*> on;
        for (const auto& o : p.osc)
            if (o.enabled && o.levelDb > engine::minLevelDb)
                on.push_back (&o);

        const bool noiseAudible = p.noise.enabled && p.noise.levelDb > engine::minLevelDb;

        if (on.empty() && ! noiseAudible)
            return {};

        juce::String text;

        if (on.empty())
        {
            text = juce::String (p.noise.levelDb > -12.0f ? "Plenty of " : "A little ") + (p.noise.pink ? "pink" : "white") + " noise";
        }
        else
        {
            text = capitalised (oscillatorList (on)) + unisonText (p);
            const auto with = extras (p, true);

            if (! with.empty())
                text += ", with " + joinList (with);
        }

        return text;
    }

    // ---- Filter, envelopes ---------------------------------------------------------------------------------

    juce::String filterText (const engine::FilterParams& f)
    {
        static const char* names[] = { "24 dB low-pass", "12 dB low-pass", "12 dB high-pass", "12 dB band-pass", "notch filter", "comb filter" };
        juce::String shaper;

        if (f.shaperEnabled && f.shaperMix > 0.01f)
        {
            static const char* shapes[] = { "saturated", "hard-clipped", "wavefolded" };
            shaper = juce::String (shapes[(int) f.shaperMode]) + " ";
        }

        auto text = ", through " + withArticle (shaper + names[(int) f.mode]) + " at " + spokenFrequency (f.cutoffHz);

        if (f.resonance >= 0.9f)
            text += ", ringing on the edge of self-oscillation";
        else if (f.resonance >= 0.6f)
            text += " with strong resonance";

        return text;
    }

    /** What the envelope sentence says about an envelope's pace; the ordinary middle range goes unmentioned. */
    juce::String paceWord (float seconds)
    {
        return seconds < 0.02f ? "fast " : seconds >= 1.5f ? "slow " : "";
    }

    /** The audible effects in chain order. The safety limiter is always on and never mentioned. */
    std::vector<juce::String> effectItems (const engine::EngineParams& p)
    {
        std::vector<juce::String> items;

        for (const auto kind : p.effectOrder)
        {
            switch (kind)
            {
                case engine::EffectKind::distortion:
                    if (p.distortion.enabled && p.distortion.mix > 0.01f)
                        items.push_back (p.distortion.driveDb < 8.0f ? "a touch of distortion" : p.distortion.driveDb >= 24.0f ? "heavy distortion" : "distortion");
                    break;

                case engine::EffectKind::chorus:
                    if (p.chorus.enabled && p.chorus.mix > 0.01f && p.chorus.depth > 0.01f)
                        items.push_back ("a chorus");
                    break;

                case engine::EffectKind::delay:
                    if (p.delay.enabled && p.delay.mix > 0.01f)
                        items.push_back (juce::String ("a ") + (p.delay.pingPong ? "ping-pong " : "") + "delay of " + params::timeText (p.delay.timeMs * 0.001f));
                    break;

                case engine::EffectKind::reverb:
                    if (p.reverb.enabled && p.reverb.mix > 0.01f)
                        items.push_back (p.reverb.decaySeconds < 1.0f ? "a short reverb" : p.reverb.decaySeconds >= 4.0f ? "a long reverb" : "a reverb");
                    break;
            }
        }

        return items;
    }

    // ---- Modulation --------------------------------------------------------------------------------------------

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

    /** One sentence per LFO: "LFO 1 wobbles the pitch gently and sweeps the cutoff slowly." */
    juce::String lfoSentence (const engine::EngineParams& p, ModSource lfo)
    {
        const juce::String name = lfo == ModSource::lfo1 ? "LFO 1" : "LFO 2";
        std::vector<juce::String> does;

        const float pitch = std::max (std::abs (amountOf (p, lfo, ModDest::pitchAll)), std::max (std::abs (amountOf (p, lfo, ModDest::pitch1)),
                            std::max (std::abs (amountOf (p, lfo, ModDest::pitch2)), std::abs (amountOf (p, lfo, ModDest::pitch3)))));

        if (pitch > 0.0f)
            does.push_back (pitch <= 0.3f ? "wobbles the pitch gently" : pitch <= 0.7f ? "wobbles the pitch" : "wobbles the pitch deeply");

        if (connected (p, lfo, { ModDest::cutoff }))
            does.push_back (p.lfo[lfo == ModSource::lfo1 ? 0 : 1].rateHz < 1.0f ? "sweeps the cutoff slowly" : "wobbles the cutoff");

        if (connected (p, lfo, { ModDest::resonance }))
            does.push_back ("moves the resonance");

        if (connected (p, lfo, { ModDest::level1, ModDest::level2, ModDest::level3, ModDest::noiseLevel, ModDest::ampLevel }))
            does.push_back ("adds a tremolo");

        if (connected (p, lfo, { ModDest::pulseWidth1, ModDest::pulseWidth2, ModDest::pulseWidth3 }))
            does.push_back ("moves the pulse width");

        if (connected (p, lfo, { ModDest::pan }))
            does.push_back ("swirls the stereo position");

        for (const auto dest : { ModDest::lfo1Rate, ModDest::lfo2Rate })
            if (const float amount = amountOf (p, lfo, dest); amount != 0.0f)
                does.push_back (juce::String (amount > 0.0f ? "speeds up " : "slows down ") + (dest == ModDest::lfo1Rate ? "LFO 1" : "LFO 2"));

        if (does.empty())
            return name + " is running but connected to nothing.";

        return name + " " + joinList (does) + ".";
    }
}

juce::String describe (const engine::EngineParams& p)
{
    auto text = sources (p);

    if (text.isEmpty())
        return "Nothing is making sound yet.";

    if (p.filter.enabled)
        text += filterText (p.filter);

    juce::String sentences = text + ".";

    // Envelopes.
    std::vector<juce::String> shaping;

    if (p.filter.enabled && p.filterEnv.enabled && std::abs (p.filterEnv.amount) >= 0.05f)
        shaping.push_back (withArticle (paceWord (std::max (p.filterEnv.attack, p.filterEnv.decay)) + "filter envelope")
                           + (p.filterEnv.amount > 0 ? " opens it" : " closes it"));

    if (p.ampEnv.enabled)
    {
        const bool snappy = p.ampEnv.attack < 0.02f && p.ampEnv.release < 0.3f;
        shaping.push_back (withArticle (snappy ? "snappy" : p.ampEnv.attack >= 0.3f ? "slow-building" : "smooth") + " amp envelope shapes each note");
    }
    else
    {
        shaping.push_back ("the keys switch it straight on and off");
    }

    sentences += " " + capitalised (joinList (shaping)) + ".";

    // Movement.
    for (const auto lfo : { ModSource::lfo1, ModSource::lfo2 })
        if (lfoPresent (p, lfo))
            sentences += " " + lfoSentence (p, lfo);

    if (connected (p, ModSource::velocity, { ModDest::cutoff }))
        sentences += " Harder keys open the filter.";

    if (connected (p, ModSource::modWheel, { ModDest::cutoff, ModDest::resonance, ModDest::pitchAll }))
        sentences += " The mod wheel takes charge of the sound.";

    if (const auto effects = effectItems (p); ! effects.empty())
        sentences += " Then " + joinList (effects) + ".";

    return sentences;
}

} // namespace scratchpad::plugin
