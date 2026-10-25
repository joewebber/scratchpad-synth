#include "EngineRig.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace enginetest;
using engine::VoiceMode;

namespace
{
    EngineParams voicingParams (VoiceMode mode)
    {
        auto p = sineParams();
        p.voicing.mode = mode;
        return p;
    }

    /** A slow attack and a low sustain, so a retrigger is easy to tell from no retrigger. */
    EngineParams slowEnvelope (VoiceMode mode, float release = 0.1f)
    {
        auto p = voicingParams (mode);
        p.ampEnv.enabled = true;
        p.ampEnv.attack = 0.2f;
        p.ampEnv.decay = 0.05f;
        p.ampEnv.sustain = 0.3f;
        p.ampEnv.release = release;
        p.ampEnv.velocitySensitivity = 0.0f;
        return p;
    }

    /** Pitch of the sine in the settled stretch after the last event. */
    double settledHz (Rig& rig)
    {
        rig.run (4800);
        return measureHz (rig.run (24000));
    }

    void requireNote (Rig& rig, int note)
    {
        INFO ("expected note " << note);
        REQUIRE (std::abs (testutil::centsBetween (settledHz (rig), testutil::midiToHz ((double) note))) < 0.5);
    }

    /** After a note change that started at the first sample of `x`, the pitch
        must slide in a straight line (in semitones) over `glideSeconds` and
        then sit on `to`. */
    void requireLinearGlide (const std::vector<float>& x, double from, double to, double glideSeconds)
    {
        const double total = glideSeconds * sampleRate;
        constexpr size_t window = 1200;

        for (double fraction : { 0.2, 0.4, 0.6, 0.8 })
        {
            const auto centre = (size_t) (fraction * total);
            const auto m = measure (x, centre - window / 2, window);
            const double expected = from + (to - from) * m.centre / total;

            INFO ("fraction " << fraction);
            REQUIRE (std::abs (semitonesOf (m.hz) - expected) < 0.1);
        }

        const auto end = measure (x, (size_t) total + 2400, 4800);
        REQUIRE (std::abs (semitonesOf (end.hz) - to) < 0.01);
    }
}

TEST_CASE ("Poly mode plays no more notes than the voice limit", "[voicing][poly]")
{
    auto p = sineParams();
    p.voicing.voices = 3;
    Rig rig (p);

    for (int n : { 60, 62, 64, 65, 67 })
    {
        rig.noteOn (n);
        rig.run (100);
    }
    rig.run (4800);

    REQUIRE (rig.activeVoices() == 3);

    // The two oldest notes were taken over; the newest three sound.
    const auto x = rig.run (32768);
    auto at = [&x] (int note)
    {
        const double hz = testutil::midiToHz ((double) note);
        return bandLevel (x, hz - 6.0, hz + 6.0);
    };

    REQUIRE (at (60) < 0.1 * at (67));
    REQUIRE (at (62) < 0.1 * at (67));
    REQUIRE (at (64) > 0.5 * at (67));
    REQUIRE (at (65) > 0.5 * at (67));
}

TEST_CASE ("Poly mode plays every held note at once", "[voicing][poly]")
{
    // Each note at -12 dB, so three together stay clear of the output limiter's knee.
    auto p = sineParams();
    p.osc[0].levelDb = -12.0f;
    Rig rig (p);

    for (int n : { 60, 64, 67 })
        rig.noteOn (n);

    rig.run (4800);
    REQUIRE (rig.activeVoices() == 3);

    const auto x = rig.run (32768);
    for (int n : { 60, 64, 67 })
    {
        const double hz = testutil::midiToHz ((double) n);
        REQUIRE (bandLevel (x, hz - 6.0, hz + 6.0) > 0.025);
    }
    REQUIRE (peak (x) > 0.375f); // more than one note's worth (0.25), which is 1.5 of 1.0 at full level
}

TEST_CASE ("Mono plays one note at a time and follows the most recent held key", "[voicing][mono]")
{
    Rig rig (voicingParams (VoiceMode::mono));

    rig.noteOn (60);
    requireNote (rig, 60);

    rig.noteOn (64);
    requireNote (rig, 64);
    REQUIRE (rig.activeVoices() == 1);

    rig.noteOff (64); // back to 60, which is still down
    requireNote (rig, 60);

    rig.noteOn (67);
    rig.noteOn (72);
    requireNote (rig, 72);
    REQUIRE (rig.activeVoices() == 1);

    rig.noteOff (72);
    requireNote (rig, 67);

    rig.noteOff (60); // not sounding: no change
    requireNote (rig, 67);

    rig.noteOff (67); // the last key: nothing left to return to
    rig.run (4800);
    REQUIRE (peak (rig.run (480)) == 0.0f);
    REQUIRE (rig.activeVoices() == 0);
}

TEST_CASE ("Legato follows the same last-note priority as mono", "[voicing][legato]")
{
    Rig rig (voicingParams (VoiceMode::legato));

    rig.noteOn (60);
    rig.noteOn (64);
    requireNote (rig, 64);
    REQUIRE (rig.activeVoices() == 1);

    rig.noteOff (64);
    requireNote (rig, 60);

    rig.noteOff (60);
    rig.run (4800);
    REQUIRE (rig.activeVoices() == 0);
}

TEST_CASE ("Mono retriggers the envelopes on every note, from where they are", "[voicing][mono][envelope]")
{
    Rig rig (slowEnvelope (VoiceMode::mono));

    rig.noteOn (60);
    rig.run (48000); // settled at the 0.3 sustain

    rig.noteOn (64);
    const auto x = rig.run (12000);

    // Not from zero: the first 10 ms still sit near the sustain level.
    REQUIRE (peakOf (x, 0, 480) > 0.25f);
    // A new attack: it climbs to full scale within the 0.2 s attack time.
    REQUIRE (peakOf (x, 0, 12000) > 0.95f);

    // Returning to a held note retriggers as well.
    rig.run (48000);
    rig.noteOff (64);
    REQUIRE (peakOf (rig.run (12000), 0, 12000) > 0.95f);
}

TEST_CASE ("Legato does not retrigger while a key is held", "[voicing][legato][envelope]")
{
    Rig rig (slowEnvelope (VoiceMode::legato));

    rig.noteOn (60);
    const auto attack = rig.run (48000);
    REQUIRE (peak (attack) > 0.95f); // the first note attacks as normal
    REQUIRE (std::abs (peakOf (attack, 43200, 2400) - 0.3f) < 0.05f);

    rig.noteOn (64); // glides on at the sustain level, no new attack
    REQUIRE (peak (rig.run (12000)) < 0.4f);

    rig.noteOff (64); // back to 60, still no attack
    REQUIRE (peak (rig.run (12000)) < 0.4f);

    rig.noteOff (60);
    rig.run (24000);
    REQUIRE (rig.activeVoices() == 0);
}

TEST_CASE ("Legato retriggers normally when no key was held", "[voicing][legato][envelope]")
{
    Rig rig (slowEnvelope (VoiceMode::legato, 1.0f));

    rig.noteOn (60);
    rig.run (48000);
    rig.noteOff (60);
    rig.run (4800); // 0.1 s into a 1 s release: still sounding
    REQUIRE (rig.activeVoices() == 1);

    rig.noteOn (64);
    REQUIRE (peak (rig.run (12000)) > 0.95f);
}

TEST_CASE ("In mono and legato the sustain pedal keeps released keys in the stack", "[voicing][pedal]")
{
    for (auto mode : { VoiceMode::mono, VoiceMode::legato })
    {
        Rig rig (voicingParams (mode));

        rig.pedal (true);
        rig.noteOn (60);
        rig.noteOn (64);
        rig.noteOff (64); // the pedal keeps it sounding
        requireNote (rig, 64);

        rig.pedal (false); // 64 leaves the stack; 60 is still down
        requireNote (rig, 60);

        rig.noteOff (60);
        rig.run (4800);
        REQUIRE (rig.activeVoices() == 0);
    }
}

TEST_CASE ("Mono and legato glide linearly in semitones over the glide time", "[voicing][glide]")
{
    for (auto mode : { VoiceMode::mono, VoiceMode::legato })
    {
        auto p = voicingParams (mode);
        p.voicing.glideSeconds = 0.5f;
        Rig rig (p);

        rig.noteOn (60); // the first note has nothing to glide from
        requireNote (rig, 60);

        rig.noteOn (72);
        requireLinearGlide (rig.run (24000 + 9600), 60.0, 72.0, 0.5);
    }
}

TEST_CASE ("Mono glides down as well as up, from the pitch it is sounding at", "[voicing][glide]")
{
    auto p = voicingParams (VoiceMode::mono);
    p.voicing.glideSeconds = 0.4f;
    Rig rig (p);

    rig.noteOn (72);
    rig.run (4800);
    rig.noteOn (60);
    const auto first = rig.run (4800); // a quarter of the way down: 72 -> 69
    const auto m = measure (first, 3000, 1200);
    REQUIRE (std::abs (semitonesOf (m.hz) - (72.0 - 12.0 * m.centre / 19200.0)) < 0.1);

    // A new note mid-glide continues from where the pitch has got to, not from the old target.
    rig.noteOn (84);
    const auto second = rig.run (2400);
    const double start = semitonesOf (measure (second, 0, 600).hz);
    REQUIRE (start < 70.0);
    REQUIRE (start > 68.0);
}

TEST_CASE ("In poly mode each voice glides from the last note it played", "[voicing][glide][poly]")
{
    auto p = sineParams();
    p.voicing.glideSeconds = 0.5f;
    p.voicing.voices = 1;
    Rig rig (p);

    rig.noteOn (60);
    requireNote (rig, 60);
    rig.noteOff (60);
    rig.run (4800); // the voice is idle now but remembers its pitch

    rig.noteOn (72);
    requireLinearGlide (rig.run (24000 + 9600), 60.0, 72.0, 0.5);
}

TEST_CASE ("A poly voice that has never played starts at its note", "[voicing][glide][poly]")
{
    auto p = sineParams();
    p.voicing.glideSeconds = 0.5f;
    Rig rig (p);

    rig.noteOn (60);
    rig.run (4800);
    rig.noteOn (72); // a different voice, which has no previous pitch
    const auto x = rig.run (4096);
    const auto window = slice (x, 1000, 2048);

    // Both pitches are already sounding; a glide from 60 would have left nothing near 523 Hz yet.
    REQUIRE (bandLevel (window, 480.0, 570.0) > 0.5 * bandLevel (window, 220.0, 310.0));
}

TEST_CASE ("Changing the voicing mode with notes held releases them cleanly", "[voicing][modes]")
{
    Rig rig;
    for (int n : { 60, 64, 67 })
        rig.noteOn (n);
    rig.run (4800);
    REQUIRE (rig.activeVoices() == 3);

    rig.set (voicingParams (VoiceMode::mono));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
    REQUIRE (rig.activeVoices() == 0);

    rig.noteOn (72); // and the new mode works
    requireNote (rig, 72);
    REQUIRE (rig.activeVoices() == 1);

    rig.set (voicingParams (VoiceMode::poly));
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);

    rig.noteOn (60);
    rig.noteOn (64);
    rig.run (4800);
    REQUIRE (rig.activeVoices() == 2);
}

TEST_CASE ("Wave-picker previews stay polyphonic in mono mode", "[voicing][preview]")
{
    auto p = voicingParams (VoiceMode::mono);
    p.osc[0].enabled = false; // the picker is only shown without oscillator 1
    Rig rig (p);

    rig.engine.previewNoteOn (57, dsp::Wave::sine);
    rig.engine.previewNoteOn (64, dsp::Wave::sine);
    rig.run (4800);
    REQUIRE (rig.activeVoices() == 2);

    rig.engine.previewNoteOff (57);
    rig.engine.previewNoteOff (64);
    rig.run (200);
    REQUIRE (peak (rig.run (480)) == 0.0f);
}
