#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/ModulationModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** The filter's response curve, 20 Hz to 20 kHz: computed from the settings, not measured, so it
    moves the instant a knob does. The cutoff is marked with a line and its value. */
class FilterCurve final : public juce::Component,
                          private juce::Timer
{
public:
    explicit FilterCurve (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

    /** y position (0 at the top) of the curve for the settings, for tests of the drawing maths. */
    static float dbToFraction (float db) noexcept;

private:
    void timerCallback() override;

    plugin::ScratchpadProcessor& processor;
    float lastCutoff = -1.0f, lastResonance = -1.0f, lastMode = -1.0f;
};

/** An ADSR drawn from its four knobs, with a dot riding the curve for the newest note. */
class EnvelopeDisplay final : public juce::Component,
                              private ModulationModel::Listener,
                              private juce::Timer
{
public:
    EnvelopeDisplay (plugin::ScratchpadProcessor&, ModulationModel&, bool isAmpEnvelope);
    ~EnvelopeDisplay() override;

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

    /** Where along the drawing (0 to 1 of its width) the dot sits for an envelope in `stage` at `level`. */
    struct Shape
    {
        float attackEnd, decayEnd, holdEnd, releaseEnd; // x fractions of the width
        float sustain;
    };

    static Shape shapeFor (float attack, float decay, float sustain, float release) noexcept;
    static float playheadX (const Shape&, int stage, float level) noexcept;

private:
    void liveUpdated() override;
    void timerCallback() override;

    plugin::ScratchpadProcessor& processor;
    ModulationModel& model;
    const bool isAmp;
    float lastA = -1, lastD = -1, lastS = -1, lastR = -1;
    float dot = -1.0f;
};

} // namespace scratchpad::ui
