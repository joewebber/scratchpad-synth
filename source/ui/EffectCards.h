#pragma once

#include "engine/EngineParams.h"
#include "plugin/Modules.h"
#include "plugin/PluginProcessor.h"
#include "ui/Knob.h"
#include "ui/ModuleCards.h"
#include "ui/ToggleChip.h"

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

/** A small live picture of one effect, computed from its settings (like `FilterCurve`), not measured:
    distortion's transfer curve, the chorus's swaying delay, the delay's echoes, the reverb's fading tail. */
class EffectDisplay final : public juce::Component,
                            private juce::Timer
{
public:
    EffectDisplay (plugin::ScratchpadProcessor&, engine::EffectKind);

    void paint (juce::Graphics&) override;
    void visibilityChanged() override;

private:
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

/** One effect: grip, name, "?", bypass and remove in the header, a picture of what it does, and its knobs.
    Effects are global, after the voices are summed, so there is no solo and no modulation. */
class EffectCard final : public ModuleCard
{
public:
    EffectCard (plugin::ScratchpadProcessor&, engine::EffectKind);

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

    engine::EffectKind getKind() const noexcept { return kind; }
    EffectGrip& getGrip() noexcept { return grip; }

    static constexpr int width = 280, height = 102;

private:
    Knob* addKnob (const char* id, const char* caption, const juce::String& accessibleSuffix, bool inRow = true);
    void showDelayMode (bool synced);
    void applyColours();

    plugin::ScratchpadProcessor& processor;
    const engine::EffectKind kind;
    juce::String name;
    EffectGrip grip;
    EffectDisplay display;

    std::vector<std::unique_ptr<Knob>> ownedKnobs;
    std::vector<Knob*> row;                     // left to right
    Knob* alternateKnob = nullptr;              // the delay's note length, in the time knob's place while synced
    std::unique_ptr<ToggleChip> syncChip, pingPongChip;
    std::unique_ptr<juce::ParameterAttachment> syncWatcher;

    juce::Rectangle<int> headerRow;
};

} // namespace scratchpad::ui
