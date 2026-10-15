#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/ChoiceSegments.h"
#include "ui/HeaderButtons.h"
#include "ui/IconButton.h"
#include "ui/Knob.h"
#include "ui/ModDial.h"
#include "ui/ModulationModel.h"
#include "ui/Visualisers.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace scratchpad::ui
{

/** A small raised card holding one module's controls. Cards for modules the
    user added have a remove button in the header row; it stays alive when the
    module goes away (the card is only hidden), so it may be clicked and the
    module removed in the same call. */
class ModuleCard : public juce::Component
{
public:
    ModuleCard();

    void paint (juce::Graphics&) override;

    /** Names the module for the Explain panel: hovering anywhere on the card explains it, and the "?" button asks. */
    void setExplainKey (const juce::String& moduleKey);

protected:
    void makeRemovable (const juce::String& accessibleName, std::function<void()> onRemove);

    /** Adds "?" and, for modules that have them, "B" (bypass) and "S" (solo, sources only) for the module
        whose parameters start with `key`. A bypassed card, or a source muted by another's solo, is dimmed. */
    void makeControllable (plugin::ScratchpadProcessor&, const juce::String& key, bool soloable, bool bypassable = true);

    /** Puts the header buttons that exist at the right end of `headerRow`, right to left: remove, "?", then
        (unless `bypassSoloElsewhere`) "S" and "B". */
    void placeRemoveButton (juce::Rectangle<int> headerRow);

    /** Where "B" and "S" go on a card whose header has no room for them. */
    void placeBypassSolo (juce::Point<int> topLeft);

    bool bypassSoloElsewhere = false;

    /** Family-coloured dot with a stroked 12 x 10 glyph, then the module name. */
    static void drawHeader (juce::Graphics&, const Palette&, juce::Rectangle<float> row,
                            juce::Colour family, const char* glyph, const juce::String& title);

    static constexpr int pad = 8;
    static constexpr int headerHeight = 22;

private:
    void refreshState();

    plugin::ScratchpadProcessor* controlled = nullptr;
    juce::String controlKey;
    bool isSoloable = false;
    std::unique_ptr<IconButton> removeButton;
    std::unique_ptr<HeaderToggle> explainButton, bypassButton, soloButton;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> stateWatchers;
};

/** Oscillator 1 to 3: wave well, wave selector, phase reset, and knobs for
    octave, semitone, fine, level and (for the pulse wave only) pulse width. */
class OscillatorCard final : public ModuleCard
{
public:
    OscillatorCard (plugin::ScratchpadProcessor&, ModulationModel&, int oscillatorIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = 448, height = 82;

private:
    void applyColours();
    void setWave (int newWave);

    const int index;
    ChoiceSegments waveSelector;
    ToggleChip phaseReset;
    Knob octave, semitone, fine, level, pulseWidth;
    juce::ParameterAttachment waveWatcher;
    int wave = 2;

    juce::Rectangle<int> waveWell, headerRow;
};

class NoiseCard final : public ModuleCard
{
public:
    NoiseCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = OscillatorCard::width, height = OscillatorCard::height;

private:
    void applyColours();

    ChoiceSegments colour;
    Knob level;
    juce::ParameterAttachment colourWatcher;
    int colourIndex = 0;

    juce::Rectangle<int> headerRow, textArea;
};

class FilterCard final : public ModuleCard
{
public:
    FilterCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = 276, height = 198;

private:
    void applyColours();

    FilterCurve curve;
    ChoiceSegments mode;
    Knob cutoff, resonance, keyTrack, drive;
    juce::Rectangle<int> headerRow;
};

/** An ADSR plus one more knob: velocity sensitivity for the amp envelope,
    amount for the filter envelope. */
class EnvelopeCard final : public ModuleCard
{
public:
    EnvelopeCard (plugin::ScratchpadProcessor&, ModulationModel&, plugin::Module which);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = FilterCard::width, height = 150;

private:
    void applyColours();

    const bool isAmp;
    EnvelopeDisplay display;
    Knob attack, decay, sustain, release, extra;
    juce::Rectangle<int> headerRow;
};

/** The amp stage. Without an amp envelope it is the fixed 2 ms on / off gate;
    with one it just says so, and the envelope card does the shaping. Its level and pan
    are always full and centred, so they are dials that only show modulation. */
class AmpCard final : public ModuleCard
{
public:
    AmpCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = FilterCard::width, height = 104;

private:
    plugin::ScratchpadProcessor& processor;
    ModDial level, pan;
    juce::Rectangle<int> textArea;
};

/** Poly, mono or legato, how many voices, glide time and pitch bend range. */
class VoicingCard final : public ModuleCard
{
public:
    explicit VoicingCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = FilterCard::width, height = 116;

private:
    void applyColours();

    ChoiceSegments mode;
    Knob voices, glide, bendRange;
    juce::ParameterAttachment modeWatcher;
    int modeIndex = 0;
    juce::Rectangle<int> headerRow;
};

class OutputCard final : public ModuleCard
{
public:
    explicit OutputCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = 78, height = 120;

private:
    Knob volume;
};

} // namespace scratchpad::ui
