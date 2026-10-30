#pragma once

#include "plugin/PluginProcessor.h"
#include "ui/Accessibility.h"
#include "ui/IconButton.h"
#include "ui/Meter.h"
#include "ui/ViewMenu.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** Patch name, New patch, Save, Open, Undo, Redo, the safety limiter's light, output meter and (standalone only)
    the route to audio settings. Scope tap and CPU readout arrive with the milestones that make them meaningful.
    Save and Open ask for a file with an asynchronous chooser that starts in the patches folder; Ctrl/Cmd+S and
    Ctrl/Cmd+O (`handleKeyPressed`, forwarded by the editor) do the same, and Ctrl/Cmd+Z undoes and
    Ctrl/Cmd+Shift+Z (or Ctrl/Cmd+Y) redoes.

    Tab visits, left to right: New patch, Save, Open, Undo, Redo, (No audio device), Settings, View options. The
    patch name, limiter light and level meter are read by a screen reader but are not Tab stops. New, save, open,
    undo, redo and failures are announced. */
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

    /** Choose a file, then save or open it. A failure shows a message and leaves the patch as it was. */
    void savePatch();
    void openPatch();

    /** Ctrl/Cmd+S and Ctrl/Cmd+O. True if the key was one of them. */
    bool handleKeyPressed (const juce::KeyPress&);

    /** The button that opens the View menu; the window gives it its `onClick`. */
    ViewMenuButton& getViewButton() noexcept { return viewButton; }

private:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override { return a11y::makeGroupHandler (*this); }
    void undoOrRedo (bool undo);
    void refreshPatchRegion();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refreshAudioDeviceChip();
    void applyColours();

    plugin::ScratchpadProcessor& processor;

    IconButton newPatchButton { "New patch", "M4 2h5.5L12.5 5v9H4z M8 7.5v4.5 M5.8 9.75h4.4" };
    IconButton savePatchButton { "Save patch", "M8 2.5v7 M5 6.8l3 3 3-3 M3 11v2.5h10V11" };
    IconButton openPatchButton { "Open patch", "M2 4h4.2l1.6 1.8H14V13H2z" };
    IconButton undoButton { "Undo", "M5 3L2 6l3 3 M2.5 6H10a3.5 3.5 0 0 1 0 7H6" };
    IconButton redoButton { "Redo", "M11 3l3 3-3 3 M13.5 6H6a3.5 3.5 0 0 0 0 7h4" };
    juce::TextButton settingsButton { "Settings" };
    IconButton noDeviceButton { "No audio device: open Settings", "M2 5h2.5L8 2v10L4.5 9H2z M10 4.5c1 .8 1 4.2 0 5",
                                14.0f, 1.3, "No audio device: open Settings" };
    ViewMenuButton viewButton;
    LimiterLight limiter;
    Meter meter;
    a11y::Region patchRegion;

    juce::Rectangle<int> patchPill;
    std::unique_ptr<juce::FileChooser> fileChooser; // alive while its dialog is
    float titleRight = 0.0f;
    juce::String lastStep { "Nothing yet" };
};

} // namespace scratchpad::ui
