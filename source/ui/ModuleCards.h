#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/ChoiceSegments.h"
#include "ui/IconButton.h"
#include "ui/Knob.h"
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

protected:
    void makeRemovable (const juce::String& accessibleName, std::function<void()> onRemove);

    /** Puts the remove button (if any) at the right end of `headerRow`. */
    void placeRemoveButton (juce::Rectangle<int> headerRow);

    /** Family-coloured dot with a stroked 12 x 10 glyph, then the module name. */
    static void drawHeader (juce::Graphics&, const Palette&, juce::Rectangle<float> row,
                            juce::Colour family, const char* glyph, const juce::String& title);

    static constexpr int pad = 8;
    static constexpr int headerHeight = 22;

private:
    std::unique_ptr<IconButton> removeButton;
};

/** Oscillator 1 to 3: wave well, wave selector, phase reset, and knobs for
    octave, semitone, fine, level and (for the pulse wave only) pulse width. */
class OscillatorCard final : public ModuleCard
{
public:
    OscillatorCard (plugin::ScratchpadProcessor&, int oscillatorIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = 448, height = 86;

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
    explicit NoiseCard (plugin::ScratchpadProcessor&);

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
    explicit FilterCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = 276, height = 146;

private:
    void applyColours();

    ChoiceSegments mode;
    Knob cutoff, resonance, keyTrack, drive;
    juce::Rectangle<int> headerRow;
};

/** An ADSR plus one more knob: velocity sensitivity for the amp envelope,
    amount for the filter envelope. */
class EnvelopeCard final : public ModuleCard
{
public:
    EnvelopeCard (plugin::ScratchpadProcessor&, plugin::Module which);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = FilterCard::width, height = 116;

private:
    void applyColours();

    const bool isAmp;
    Knob attack, decay, sustain, release, extra;
    juce::Rectangle<int> headerRow;
};

/** The amp stage. Without an amp envelope it is the fixed 2 ms on / off gate;
    with one it just says so, and the envelope card does the shaping. */
class AmpCard final : public ModuleCard
{
public:
    explicit AmpCard (plugin::ScratchpadProcessor&);

    void paint (juce::Graphics&) override;

    static constexpr int width = FilterCard::width, height = 100;

private:
    plugin::ScratchpadProcessor& processor;
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
