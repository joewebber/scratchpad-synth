#pragma once

#include "dsp/Oscillator.h"

#include <juce_graphics/juce_graphics.h>

namespace scratchpad::ui
{

/** `cycles` cycles of the ideal wave, fitted into `area`; `amplitude` is the
    fraction of the half-height the wave reaches. */
juce::Path wavePath (dsp::Wave, juce::Rectangle<float> area, float cycles, float amplitude);

} // namespace scratchpad::ui
