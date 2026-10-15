#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/LookAndFeel.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace scratchpad::ui
{

/** The shared scope: the waveform or the spectrum of one tap in the signal path (after the oscillators,
    after the filter, after the amp, after the effects). The audio thread queues the samples; this drains
    them 30 times a second, keeps the last 8,192 and draws from those. The spectrum is a 2,048-point
    Hann-windowed FFT run here on the message thread, never in the audio callback.

    A caption under it says in words what it is showing. */
class Scope final : public juce::Component,
                    private juce::Timer
{
public:
    explicit Scope (plugin::ScratchpadProcessor&);
    ~Scope() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    enum class Mode { wave, spectrum };

    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, historySize = engine::scopeFifoSize;
    static constexpr int numBars = 72;

    /** Seconds-independent helpers, public so they can be tested. */
    static int findTrigger (const float* x, int count, int searchFrom, int searchTo);

private:
    void timerCallback() override;
    void setMode (Mode);
    void setTap (engine::ScopeTap);
    void drawWave (juce::Graphics&, juce::Rectangle<float>);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    juce::String captionText() const;
    juce::Colour tapColour (const Palette&) const;

    class Segments final : public juce::Component
    {
    public:
        Segments (juce::StringArray labelsToShow, std::function<void (int)> onPick);
        void setSelected (int i, juce::Colour fill);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

    private:
        juce::StringArray labels;
        std::function<void (int)> pick;
        int selected = 0;
        juce::Colour fillColour;
    };

    plugin::ScratchpadProcessor& processor;
    Mode mode = Mode::wave;
    engine::ScopeTap tap = engine::ScopeTap::amp;

    Segments modeButtons, tapButtons;
    juce::Rectangle<int> display, captionArea;

    std::array<float, historySize> history {};
    int written = 0; // newest sample is at (written - 1) % historySize
    std::vector<float> scratch;

    juce::dsp::FFT fft { fftOrder };
    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize> window {};
    std::array<float, numBars> bars {};
    std::array<float, numBars> peaks {};
    std::array<float, 1024> shownWave {};
    float waveScale = 1.0f;
    bool hasSignal = false;
};

} // namespace scratchpad::ui
