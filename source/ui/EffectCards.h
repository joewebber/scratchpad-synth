#pragma once

#include "engine/EngineParams.h"
#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/Knob.h"
#include "ui/ModuleCard.h"
#include "ui/MorePopover.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace scratchpad::ui
{

/** The module an effect is added and removed as. */
inline plugin::Module effectModule (engine::EffectKind kind) noexcept
{
    return (plugin::Module) ((int) plugin::Module::distortion + (int) kind);
}

/** A small live picture of one effect (the card's 56 x 40 well), computed from its settings (like `FilterCurve`),
    not measured: distortion's transfer curve, the chorus's swaying delay, the delay's echoes, the reverb's fading
    tail. Distortion and delay add a one-word tag at the bottom right (the mode; the time or note length). */
class EffectDisplay final : public juce::Component,
                            private juce::Timer
{
public:
    EffectDisplay (plugin::ScratchpadProcessor&, engine::EffectKind);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::String describe() const;

    using Values = std::array<float, 8>;

    void timerCallback() override;
    Values read() const;

    void paintDistortion (juce::Graphics&, juce::Rectangle<float> area, const Values&) const;
    void paintChorus (juce::Graphics&, juce::Rectangle<float> area, const Values&) const;
    void paintDelay (juce::Graphics&, juce::Rectangle<float> area, const Values&) const;
    void paintReverb (juce::Graphics&, juce::Rectangle<float> area, const Values&) const;

    plugin::ScratchpadProcessor& processor;
    const engine::EffectKind kind;
    Values shown {};
};

/** The drag handle at the left of an effect card's header. Drag it sideways to change where the effect sits in
    the chain, or press Left or Right (Up or Down) while it has the keyboard focus. It only reports; the chain
    changes through `ScratchpadProcessor::moveEffect`, which the owner calls. */
class EffectGrip final : public juce::Button
{
public:
    explicit EffectGrip (const juce::String& effectName);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (const juce::MouseEvent&)> onDragStart, onDragMove, onDragEnd;

    /** -1 to move one place towards the start of the chain, +1 towards the output. */
    std::function<void (int)> onNudge;
};

/** One effect (`layout::fx[i]`, 184 x 80): in the header the grip, the effect's place in the chain, its name, "?", B
    and More; below it a 56 x 40 picture of what it does and three knobs. What does not fit (distortion mode and bias,
    the delay's sync and ping-pong, the reverb's damping) is behind More. While the delay is synced its Time knob is
    replaced, in the same place, by the note length. Effects are global, after the voices are summed, so there is no
    solo and no modulation. */
class EffectCard final : public ModuleCard
{
public:
    EffectCard (plugin::ScratchpadProcessor&, engine::EffectKind);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    engine::EffectKind getKind() const noexcept { return kind; }
    EffectGrip& getGrip() noexcept { return grip; }

    /** Where this effect is among the effects in the patch (1 is first): the number in the header and the grip's description. */
    void setPosition (int position, int count);

private:
    Knob* addKnob (const char* id, const char* caption, const juce::String& accessibleSuffix, bool inRow = true);
    void showDelayMode (bool synced);
    void fillMore (MorePopover&) const;
    void applyColours();

    plugin::ScratchpadProcessor& processor;
    const engine::EffectKind kind;
    juce::String name;
    EffectGrip grip;
    EffectDisplay display;
    int position = 0;

    std::vector<std::unique_ptr<Knob>> ownedKnobs;
    std::vector<Knob*> row;                     // left to right
    Knob* alternateKnob = nullptr;              // the delay's note length, in the time knob's place while synced
    std::unique_ptr<juce::ParameterAttachment> syncWatcher;

    juce::Rectangle<int> headerRow;
};

} // namespace scratchpad::ui
