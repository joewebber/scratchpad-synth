#pragma once

#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/ChoiceSegments.h"
#include "ui/HeaderButtons.h"
#include "ui/Knob.h"
#include "ui/KeyboardSlider.h"
#include "ui/Layout.h"
#include "ui/ModulationModel.h"
#include "ui/ModuleCard.h"
#include "ui/MorePopover.h"
#include "ui/ToggleChip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** The WAVE cell of an oscillator row: a 52 x 30 well drawing the wave, its name under it. It is the wave picker:
    a click, Space or Enter opens a menu of the seven waves, and the arrow keys step through them. To a screen
    reader it is one selector named for the oscillator ("Oscillator 1 wave"), whose value is the wave. */
class WaveChooser final : public juce::Component
{
public:
    WaveChooser (const juce::String& accessibleName, std::function<juce::String()> describe);

    void setWave (int newWave);
    int getWave() const noexcept { return wave; }

    /** Called with the wave the user picked; the owner writes it. */
    std::function<void (int)> onPick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { setHovered (true); }
    void mouseExit (const juce::MouseEvent&) override { setHovered (false); }
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    void setHovered (bool shouldBeHovered);
    void showMenu();

    std::function<juce::String()> describe;
    int wave = 2;
    bool hovered = false;
};

/** One row of the Sources table, not a card of its own: the table's raised card is the `SourcesCard`. A row has the
    module's name with its "?", B and S under it, its body cells, and its "..." More button. */
class SourceRow : public ModuleCard
{
public:
    void paint (juce::Graphics&) override;

    static constexpr int nameColumn = layout::table::nameColumn, columnGap = 6, columnWidth = 348;

protected:
    SourceRow (plugin::ScratchpadProcessor&, const juce::String& title, const juce::String& shortName, const juce::String& key);

    /** Finds the "?", B and S that `makeControllable` made; call once the constructor has called `makeMore` too. */
    void adoptChrome();

    /** Name, "?", B, S in the name column and More at `moreX`; the row's body is laid out by the subclass. */
    void layoutChrome (int rowHeight, int moreX);

    plugin::ScratchpadProcessor& processor;

private:
    juce::String shortName;
    int height = 0;
    HeaderToggle *explain = nullptr, *bypass = nullptr, *solo = nullptr;
};

/** Oscillator 1, 2 or 3: wave, Oct, Semi, Fine, Level and Morph cells (Morph only for the pulse and wavetable
    waves). More holds phase reset, unison and sub level, anti-aliasing, hard sync (Osc 2 and 3) or what Osc 1
    does to the others (ring, FM, FM target), and Remove for Osc 2 and 3. Ring and FM act on Osc 2 or 3, so they
    dim (and say why in a line at the bottom) while those oscillators are not in the patch. */
class OscillatorCard final : public SourceRow
{
public:
    OscillatorCard (plugin::ScratchpadProcessor&, ModulationModel&, int oscillatorIndex);

    void resized() override;
    void lookAndFeelChanged() override;

    static constexpr int rowHeight = layout::table::rowHeight;

private:
    void applyColours();
    void fillMore (MorePopover&);
    void setWave (int newWave);
    void refreshExtras();
    juce::String statusSentence() const;
    float raw (const char* id) const;

    const int index;
    WaveChooser waveChooser;
    Knob octave, semitone, fine, level, morph;
    juce::ParameterAttachment waveAttachment;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> extrasWatchers;
    int wave = 2;
    bool aliasingOff = false;
};

/** The noise row: colour, a sentence about it, level; More holds Remove. */
class NoiseCard final : public SourceRow
{
public:
    NoiseCard (plugin::ScratchpadProcessor&, ModulationModel&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** What is left of the card under the three oscillator rows. */
    static constexpr int rowHeight = layout::sources.h - 2 * layout::padding::sources - layout::table::headingHeight
                                     - layout::table::headingMargin - 3 * layout::table::rowHeight;

private:
    void applyColours();
    void refreshText();

    ChoiceSegments colour;
    Knob level;
    juce::ParameterAttachment colourWatcher;
    a11y::Region textRegion;
    int colourIndex = 0;
    juce::Rectangle<int> textArea;
};

/** The Sources card (`layout::sources`): the oscillator table, then noise. Absent Osc 2, Osc 3 and noise show a
    dashed "+ Osc 2" slot in their row's place (rows keep their height). The card owns presence: `PatchView` calls
    `refreshPresence` when modules come and go, and sets `onAddModule` for the slots.

    To a screen reader it is a group named "Sources". */
class SourcesCard final : public juce::Component
{
public:
    SourcesCard (plugin::ScratchpadProcessor&, ModulationModel&);

    /** Called when a dashed slot is pressed, with the module it stands for (Osc 2, Osc 3 or Noise). */
    std::function<void (plugin::Module)> onAddModule;

    /** Shows each row, or its slot, as `processor.isModulePresent` says. Oscillator 1 is always there. */
    void refreshPresence();

    /** The keyboard goes to the new row's first control (after the user added it). */
    void focusModule (plugin::Module);

    /** The keyboard goes to the dashed slot that took a removed row's place. */
    void focusSlot (plugin::Module);

    /** True for the modules that live in this card (Osc 2, Osc 3, Noise; Osc 1 is not a `Module`). */
    static bool holds (plugin::Module) noexcept;

    /** Oscillator 1's row, which is always there. */
    ModuleCard& oscillator1() noexcept { return osc1; }

    /** The row for `module` while it is in the patch, else null. */
    ModuleCard* rowFor (plugin::Module);

    /** The slot for `module` while it is absent, else null. */
    juce::Component* slotFor (plugin::Module);

    /** What the card shows, top to bottom: each row, or its slot where the module is absent. */
    std::vector<juce::Component*> focusOrder();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::Rectangle<int> rowArea (int row) const;
    juce::Rectangle<int> slotArea (int row) const;

    plugin::ScratchpadProcessor& processor;
    OscillatorCard osc1, osc2, osc3;
    NoiseCard noise;
    SlotButton osc2Slot, osc3Slot, noiseSlot;
};

} // namespace scratchpad::ui
