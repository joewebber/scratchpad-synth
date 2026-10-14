#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/IconButton.h"
#include "ui/Meter.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** Patch name, New patch, Undo, Redo, output meter and (standalone only) the
    route to audio settings. Save, Open, scope tap, limiter light and CPU
    readout arrive with the milestones that make them meaningful. */
class TopBar final : public juce::Component,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    explicit TopBar (plugin::ScratchpadProcessor&);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override { applyColours(); }
    void parentHierarchyChanged() override { applyColours(); }

    /** Updates the "Last: ..." line and the Undo/Redo enabled state. */
    void refreshHistory();

    /** On the wave picker the bar shows "New patch" as a plain caption and
        hides the patch controls, as in the prototype. */
    void setPickerMode (bool picking);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshAudioDeviceChip();
    void applyColours();

    plugin::ScratchpadProcessor& processor;

    IconButton newPatchButton { "New patch: back to the wave picker", "M4 2h5.5L12.5 5v9H4z M8 7.5v4.5 M5.8 9.75h4.4" };
    IconButton undoButton { "Undo", "M5 3L2 6l3 3 M2.5 6H10a3.5 3.5 0 0 1 0 7H6" };
    IconButton redoButton { "Redo", "M11 3l3 3-3 3 M13.5 6H6a3.5 3.5 0 0 0 0 7h4" };
    juce::TextButton settingsButton { "Settings" };
    IconButton noDeviceButton { "No audio device: open Settings", "M2 5h2.5L8 2v10L4.5 9H2z M10 4.5c1 .8 1 4.2 0 5",
                                14.0f, 1.3, "No audio device: open Settings" };
    Meter meter;

    bool picking = false;
    juce::Rectangle<int> patchPill, newPatchCaption;
    float titleRight = 0.0f;
    juce::String lastStep { "Nothing yet" };
};

} // namespace scratchpad::ui
