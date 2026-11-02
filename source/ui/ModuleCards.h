#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/Layout.h"
#include "ui/Meter.h"
#include "ui/ModDial.h"
#include "ui/ModulationModel.h"
#include "ui/ModulationStrip.h"
#include "ui/ModuleCard.h"
#include "ui/Motion.h"
#include "ui/MorePopover.h"
#include "ui/SourcesCard.h"
#include "ui/Visualisers.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace scratchpad::ui
{

/** `layout::filter`: response curve with its readout, mode, and Cutoff, Res, Key and Drive. It lays itself out from
    its own box, in one of two whole layouts (`isFull`): compact, and full from `layout::full::filterWidth` x
    `filterHeight`, which adds under the knob row the per-voice shaper (a Shape segmented control, Off - Tanh - Hard
    clip - Wavefold, and its Shape (drive), Bias and Mix knobs). The curve takes the extra width and height; the knobs
    keep their size and their distance from the card's edges. More holds Remove, and (compact only) the shaper:
    those controls move between the card and More as the layout changes, they are never in both. */
class FilterCard final : public ModuleCard
{
public:
    FilterCard (plugin::ScratchpadProcessor&, ModulationModel&);

    /** True while the card's box is big enough for the full layout (the shaper is on the card, not in More). */
    bool isFull() const noexcept { return full; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void applyColours();
    void followShaper();

    FilterCurve curve;
    ChoiceSegments mode;
    Knob cutoff, resonance, keyTrack, drive;
    ChoiceSegments shape;
    Knob shaperDrive, shaperBias, shaperMix;
    juce::RangedAudioParameter &shaperEnabled, &shaperMode;
    juce::ParameterAttachment shaperEnabledWatcher, shaperModeWatcher;
    juce::Rectangle<int> headerRow;
    bool full = false;
};

/** `layout::filterEnv` or `layout::ampEnv`: an ADSR plus one more knob (velocity sensitivity for the amp envelope,
    amount for the filter envelope). The header carries the envelope's drag handle (a modulation source) in the
    place of the family dot. More holds Remove. The display well takes the card's extra width and height; the five
    knobs keep their size and their distance from the right and bottom edges. */
class EnvelopeCard final : public ModuleCard
{
public:
    EnvelopeCard (plugin::ScratchpadProcessor&, ModulationModel&, plugin::Module which);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void applyColours();

    const bool isAmp;
    SourceHandle handle;
    EnvelopeDisplay display;
    Knob attack, decay, sustain, release, extra;
    juce::Rectangle<int> headerRow;
};

/** One bar per voice the synth may play, filled to that voice's amp level: which voices are sounding right now.
    Fed by the engine's live feed (`LiveModulation::voiceLevel`), polled on the display's vertical blank. */
class VoiceActivity final : public juce::Component
{
public:
    explicit VoiceActivity (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void tick();
    int voiceCount() const;
    juce::String summary() const;

    plugin::ScratchpadProcessor& processor;
    std::array<float, engine::VoiceAllocator::maxVoices> shown {};
    int sounding = 0;
    motion::Throttle throttle;
    juce::VBlankAttachment vblank;
};

/** `layout::amp`: the voices that are sounding, and the fixed Level and Pan dials that show what modulation does
    to them. No More button: there is nothing behind it. The voice well takes the card's extra width and height; the
    knobs keep their size and place (bottom left). */
class AmpCard final : public ModuleCard
{
public:
    AmpCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    VoiceActivity voices;
    ModDial level, pan;
    juce::Rectangle<int> headerRow;
};

/** `layout::out`: Volume, the vertical output meter and the safety limiter's light (`control.limiter`). The meter
    takes the extra height; the knob, meter width and limiter light keep their size and place. */
class OutputCard final : public ModuleCard
{
public:
    explicit OutputCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    Knob volume;
    LimiterLight limiter;
    Meter meter;
};

} // namespace scratchpad::ui
