#pragma once

#include "dsp/HalfbandDecimator.h"
#include "dsp/Oscillator.h"

#include <array>
#include <cstdint>
#include <numbers>

namespace scratchpad::engine
{

/** Copies per oscillator in unison. */
inline constexpr int maxUnison = 7;

/** What one oscillator does for the coming block. The voice builds these from the patch. */
struct BankSettings
{
    bool on = false;                // false: not run, silent (the voice has no use for it)
    dsp::Wave wave = dsp::Wave::sine;
    double increment = 0.0;         // cycles per sample at the end of the block, before unison detune
    float pulseWidth = 0.5f;        // at the end of the block
    int unison = 1;                 // 1 .. maxUnison copies
    float detuneCents = 0.0f;       // the outer copies sit this far either side
    float spread = 0.0f;            // 0 .. 1 stereo spread of the copies
    bool antiAlias = true;
    bool phaseReset = true;         // a note starts at phase 0 (the first copy; the others at random phases)
    bool sync = false;              // restart whenever oscillator 1 wraps (oscillators 2 and 3)
};

struct StereoSample
{
    float l = 0.0f;
    float r = 0.0f;
};

using StereoOscs = std::array<StereoSample, 3>;

/** The three oscillators of one voice with everything that links them: unison copies, the
    anti-aliasing switch, hard sync from oscillator 1, ring modulation's inputs and two-operator
    FM. It makes stereo audio per oscillator; levels, ring and the mix are the voice's job.

    An `OscSection` runs at the base sample rate (`oversampling` 1) or at twice that with a
    half-band decimator after each oscillator (`oversampling` 2). The voice runs the second
    whenever FM is on, crossfading to and from the first. Both arrive with the same delay:
    `Oscillator::latencySamples` samples at the base rate, because the oversampled oscillators'
    own delay is half as long (in base-rate samples) and the decimator's makes up the rest.

    Unison. Copy `c` of `n` sits at `u = 2c / (n - 1) - 1` (-1 .. +1). It is detuned by
    `u * detuneCents` and panned to `u * spread` with an equal-power law (a centred copy is
    left and right at unity, so one copy is exactly the single oscillator). Copies are scaled by
    `1 / sqrt (n)`, which keeps the loudness about the same for copies at random phases. Gains and
    detune move smoothly (3 ms one-pole for the gains, linear across the block for the detune), so
    changing the count or spread never clicks; a copy that is added starts at a random phase from
    silence, one that is removed fades out first.

    Hard sync. A synced copy restarts its cycle when the matching oscillator-1 copy wraps. The
    restart is placed to a fraction of a sample and corrected with a BLEP (and BLAMP) at the jump.
    Slave copy `c` listens to master copy `c` scaled onto the master's copies (the centre one when
    the slave has a single copy).

    FM. Oscillator 1 phase-modulates the oscillators in `fmTo2`/`fmTo3`, DX-style: the carrier's
    phase is advanced by `index * m(t) / (2 pi)` cycles, where `m` is the modulator (oscillator 1's
    naive wave, its copies' gains applied, unit amplitude) and `index` is the peak phase
    deviation in radians: `fmMaxIndex * amount`, so 100% is a modulation index of 10.
    The sidebands of a sine modulator at `fm` around a carrier at `fc` are `fc +- k * fm` with
    Bessel-function weights J_k(index). The modulator is the naive wave because the band-limited one
    would arrive `latencySamples` late and the carrier would then be late twice. */
class OscSection
{
public:
    static constexpr double fmMaxIndex = 10.0; // radians of peak phase deviation at an FM amount of 100%
    static constexpr double fmCyclesAtFull = fmMaxIndex / (2.0 * std::numbers::pi);
    static constexpr double gainSmoothingSeconds = 0.003;
    static constexpr double switchSeconds = 0.010; // wave changes and the anti-aliasing switch crossfade over this

    /** `oversampling` is 1 or 2. `seed` drives the random start phases. */
    void prepare (double sampleRate, int oversampling, std::uint32_t seed) noexcept;

    /** Stops everything and readies the copies for a new note: copy 0 of each oscillator at phase 0
        (as the single oscillator always was), the others at scattered phases for notes that do not reset. */
    void clear() noexcept;

    /** Stops every oscillator so the next block starts them afresh (a new note). */
    void stop() noexcept;

    /** Applies the settings for the next `numSamples` base-rate samples. Oscillators that were not
        running start; the others move to the new increment in a straight line across the block. */
    void beginBlock (const std::array<BankSettings, 3>& settings, int numSamples) noexcept;

    /** One base-rate sample. `fmTo2` and `fmTo3` are the FM amounts (0 .. 1) for oscillators 2 and 3. */
    void tick (float fmTo2, float fmTo3, StereoOscs& out) noexcept;

    /** Takes over the state of `other` (phases, gains, wave, unison) so a crossfade can start from it. */
    void adoptFrom (const OscSection& other) noexcept;

    /** True while any copy is panned off centre, or still moving from or to it. */
    bool needsStereo() const noexcept;

private:
    struct Copy
    {
        dsp::Oscillator osc;
        double ratio = 1.0;       // frequency ratio from detune
        double ratioStep = 0.0;   // per tick
        float gainL = 0.0f, gainR = 0.0f;
        float targetL = 0.0f, targetR = 0.0f;
        int masterCopy = 0;
        bool active = false;
    };

    struct Bank
    {
        std::array<Copy, maxUnison> copies;
        dsp::Wave wave = dsp::Wave::sine;
        dsp::Wave waveFrom = dsp::Wave::sine;
        int fadeRemaining = 0;
        double increment = 0.0;
        double incrementStep = 0.0; // per tick
        float pulseWidth = 0.5f;
        float widthStep = 0.0f;     // per tick
        float bandLimit = 1.0f;
        float bandLimitTarget = 1.0f;
        int count = 1;
        bool sync = false;
        bool running = false;
    };

    void beginBank (size_t index, const BankSettings& s, int numSamples) noexcept;
    void stopBank (size_t index) noexcept;
    void step (float fmTo2, float fmTo3, StereoOscs& out) noexcept;
    StereoSample tickBank (Bank& b, const Bank* master, double phaseOffset) noexcept;
    float modulator() const noexcept;
    double random() noexcept;

    static int masterCopyFor (int copy, int copies, int masterCopies) noexcept;

    std::array<Bank, 3> banks;
    std::array<std::array<dsp::HalfbandDecimator, 2>, 3> decimators;

    int oversampling = 1;
    double rateScale = 1.0;      // increment of one tick relative to the base rate
    int fadeTicks = 480;         // 10 ms
    float bandLimitStep = 0.01f; // per tick
    float gainCoef = 0.05f;      // per tick
    std::uint32_t rng = 1u;
};

} // namespace scratchpad::engine
