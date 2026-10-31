#pragma once

#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModuleCard.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** Poly, mono or legato (`layout::segments::voiceMode` wide) with its "Voicing" caption over it, and to the right how
    many voices, glide time, pitch-bend range and drift as four `layout::knobs::voicing` knobs. It has no card of its
    own and no menu: it sits at the left of the performance bar's card, which also holds the wheels and the keys (the
    voicing sits with the keyboard because it changes how the keys play). A screen reader finds it as a group named
    "Voicing". */
class VoicingCard final : public ModuleCard
{
public:
    explicit VoicingCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** What the controls need: the segments, then the four knobs 4 px apart, 16 px after them. */
    static constexpr int width = layout::segments::voiceMode + 16 + 4 * layout::knobs::voicing.cell + 3 * 4, height = 100;

private:
    void applyColours();

    ChoiceSegments mode;
    Knob voices, glide, bendRange, drift;
    juce::ParameterAttachment modeWatcher;
    int modeIndex = 0;
    juce::Rectangle<int> captionArea;
};

} // namespace scratchpad::ui
