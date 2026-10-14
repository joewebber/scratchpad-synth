#pragma once

#include "dsp/Oscillator.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace scratchpad::ui
{

/** The first thing anyone sees: four waves, each with a drawn cycle, its
    harmonics and a one-line description. Hovering or focusing a wave holds a
    note so the user hears it before choosing.

    The picker never talks to the audio engine itself. `onPreview` is told
    which wave to hold (or -1 to stop) and `onChoose` which wave was picked. */
class WavePicker final : public juce::Component
{
public:
    /** MIDI note held for previews: A3, 220 Hz. */
    static constexpr int previewNote = 57;

    WavePicker();
    ~WavePicker() override;

    std::function<void (int wave)> onPreview; // wave index, or -1 for none
    std::function<void (int wave)> onChoose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    int getPreviewingWave() const noexcept { return previewing; }

private:
    class Card;

    void setHighlighted (int wave);
    void startPreview (int wave);
    void stopPreview (int wave);
    void moveFocus (int delta);
    juce::String statusText() const;

    std::array<std::unique_ptr<Card>, dsp::numWaves> cards;
    juce::Rectangle<int> statusStrip;
    int previewing = -1;
    int highlighted = 2; // saw: the prototype's default emphasis

    friend class Card;
};

} // namespace scratchpad::ui
