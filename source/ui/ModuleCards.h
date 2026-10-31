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

/** `layout::filter`: response curve with its readout, mode, and Cutoff, Res, Key and Drive. More holds the
    per-voice shaper (on or off, shape, drive, bias and mix) and Remove. */
class FilterCard final : public ModuleCard
{
public:
    FilterCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void applyColours();

    FilterCurve curve;
    ChoiceSegments mode;
    Knob cutoff, resonance, keyTrack, drive;
    juce::Rectangle<int> headerRow;
};

/** `layout::filterEnv` or `layout::ampEnv`: an ADSR plus one more knob (velocity sensitivity for the amp envelope,
    amount for the filter envelope). The header carries the envelope's drag handle (a modulation source) in the
    place of the family dot. More holds Remove. */
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
    to them. No More button: there is nothing behind it. */
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

/** `layout::out`: Volume, the vertical output meter and the safety limiter's light (`control.limiter`). */
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
