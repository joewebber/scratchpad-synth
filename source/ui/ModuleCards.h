#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
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
#include <vector>

namespace scratchpad::ui
{

/** A small raised card holding one module's controls. Cards for modules the
    user added have a remove button in the header row; it stays alive when the
    module goes away (the card is only hidden), so it may be clicked and the
    module removed in the same call.

    To a screen reader a card is a named group (its title is the module's name, its description what the module
    does) holding its controls. Tab visits, in this order: bypass, solo, explain and remove (those the card has),
    then the card's own controls in the order its constructor gives (`setFocusOrder`). */
class ModuleCard : public juce::Component
{
public:
    ModuleCard();

    void paint (juce::Graphics&) override;

    /** Names the module for the Explain panel: hovering anywhere on the card explains it, and the "?" button asks. */
    void setExplainKey (const juce::String& moduleKey);

    /** Puts the keyboard on the card's first control (what a newly added module offers first). */
    void focusFirstControl();

protected:
    /** The card's own controls in Tab order, after the header buttons. The first is where `focusFirstControl` lands,
        unless `entry` says otherwise. */
    void setFocusOrder (const std::vector<juce::Component*>& controls, juce::Component* entry = nullptr);

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

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
    juce::Component::SafePointer<juce::Component> entryControl;
};

/** The small chevron on an oscillator card that opens its extras (unison, anti-aliasing, sync, and ring and FM on
    Oscillator 1). A dot marks a card whose extras are doing something: calm for unison, sync, ring or FM, in the
    accent colour when anti-aliasing is off. To a screen reader it is a toggle button: "pressed" while the extras
    are open. */
class MoreButton final : public juce::Button
{
public:
    enum class Marker { none, inUse, warning };

    explicit MoreButton (const juce::String& oscillatorName);

    void setOpen (bool shouldBeOpen);
    void setMarker (Marker);
    bool isOpen() const noexcept { return open; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void refreshTitle();

    juce::String name;
    bool open = false;
    Marker marker = Marker::none;
};

/** Oscillator 1 to 3: wave well, wave selector, phase reset, and knobs for
    octave, semitone, fine, level and (for the pulse wave only) pulse width. A chevron beside the
    phase reset switch opens the oscillator's extras, which PatchView lays out as a drawer under the card
    (see `OscillatorExtras`); with anti-aliasing off the wave well wears an accent outline and an "AA off" tag. */
class OscillatorCard final : public ModuleCard
{
public:
    OscillatorCard (plugin::ScratchpadProcessor&, ModulationModel&, int oscillatorIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** Called when the chevron is pressed. The owner decides what opens; it tells the card with `setMoreOpen`. */
    std::function<void()> onMoreClicked;
    void setMoreOpen (bool isOpen) { moreButton.setOpen (isOpen); }

    /** Puts the keyboard on the chevron (where it goes back to when its drawer is closed with Escape). */
    void focusMoreButton() { moreButton.grabKeyboardFocus(); }

    static constexpr int width = 448, height = 82;

private:
    void applyColours();
    void setWave (int newWave);
    void refreshExtras();
    float raw (const char* id) const;

    const int index;
    plugin::ScratchpadProcessor& processor;
    ChoiceSegments waveSelector;
    ToggleChip phaseReset;
    Knob octave, semitone, fine, level, pulseWidth;
    MoreButton moreButton;
    juce::ParameterAttachment waveWatcher;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> extrasWatchers;
    int wave = 2;
    bool aliasingOff = false;

    a11y::Region waveRegion { a11y::Region::Kind::image };
    juce::Rectangle<int> waveWell, headerRow;
};

/** What an oscillator has behind its chevron: unison (copies, detune, spread) and the anti-aliasing switch on all
    three, hard sync on Oscillators 2 and 3, and on Oscillator 1 the things it does to the others: ring
    modulation level, FM amount and FM target. It acts on Oscillator 2 or 3, so those controls dim (and say
    why in the line at the bottom) while the oscillators they need are not in the patch. PatchView shows it as
    a drawer over the cards below its oscillator. */
class OscillatorExtras final : public ModuleCard
{
public:
    OscillatorExtras (plugin::ScratchpadProcessor&, int oscillatorIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int width = OscillatorCard::width, height = 100;

    /** Called when Escape is pressed anywhere in the drawer: the owner closes it. */
    std::function<void()> onClose;

    bool keyPressed (const juce::KeyPress&) override;

private:
    void applyColours();
    void watch (const char* id);
    void refreshState();
    float raw (const char* id) const;

    plugin::ScratchpadProcessor& processor;
    const int index;
    Knob unison, detune, spread;
    ToggleChip antiAlias;
    std::unique_ptr<ToggleChip> sync;            // Oscillators 2 and 3
    std::unique_ptr<Knob> ring, fm;              // Oscillator 1
    std::unique_ptr<ChoiceSegments> fmTarget;    // Oscillator 1
    std::vector<std::unique_ptr<juce::ParameterAttachment>> watchers;

    a11y::Region statusRegion;
    juce::String status;
    bool statusIsWarning = false;
    juce::Rectangle<int> statusArea;
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
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

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
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

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
