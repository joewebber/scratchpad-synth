#pragma once

#include "dsp/Oscillator.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace scratchpad::ui
{

juce::String waveName (dsp::Wave);
juce::String waveTag (dsp::Wave);
juce::String waveDescription (dsp::Wave);

/** `cycles` cycles of the ideal wave, fitted into `area`; `amplitude` is the
    fraction of the half-height the wave reaches. */
juce::Path wavePath (dsp::Wave, juce::Rectangle<float> area, float cycles, float amplitude);

/** Linear amplitude of harmonics 1..count relative to the fundamental's
    unit-scale (sine: 1,0,0..; saw: 1/n; pulse at 50%: 1/n odd only;
    triangle: 1/n^2 odd only). */
std::vector<float> harmonicAmplitudes (dsp::Wave, int count);

/** Draws harmonics as vertical bars on a 48 dB scale, as in the prototype. */
void drawHarmonics (juce::Graphics&, juce::Rectangle<float> area, dsp::Wave, int count, juce::Colour);

} // namespace scratchpad::ui
