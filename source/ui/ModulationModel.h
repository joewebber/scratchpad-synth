#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace scratchpad::ui
{

class ModTarget;

/** The range a source swings over: LFOs and key are bipolar, the rest 0 to 1. */
struct SourceRange
{
    float low, high;
};

SourceRange sourceRange (engine::ModSource);

/** A destination's value, in the units of the knob that shows it (cents, percent, dB, Hz,
    percent, percent), after the connections that drive it add up to `sum`. The scales are
    the engine's: see `engine/Modulation.h`. */
float modulatedValue (engine::ModDest, float base, float sum);

/** One concentric arc on a knob: where a connection can move it, and where it is now.
    Positions are fractions of the knob's travel, 0 to 1. */
struct ModArc
{
    int slot;                 // -1 for the filter envelope's own, hard-wired amount
    engine::ModSource source;
    float amount;             // -1 .. +1
    float low, high, live;
};

/** The editor's view of modulation: the eight connections as they stand, the live source
    values from the audio thread, and the state of a connection being made (dragged, or
    armed by a click). Every control that can be a destination registers itself here so a
    handle's drop can find it.

    Message thread only. */
class ModulationModel final : private juce::ChangeListener,
                              private juce::Timer
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void modulationChanged() {}   // a slot's source, destination or amount changed
        virtual void connectingChanged() {}   // a drag or an armed handle started or ended
        virtual void liveUpdated() {}         // new live values, about 30 times a second
        virtual void statusChanged() {}       // the one-line message changed
    };

    explicit ModulationModel (plugin::ScratchpadProcessor&);
    ~ModulationModel() override;

    plugin::ScratchpadProcessor& getProcessor() noexcept { return processor; }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Live updates only run while the synth is on screen. */
    void setActive (bool shouldRun);

    const engine::ModConnections& getConnections() const noexcept { return connections; }
    int countConnections() const noexcept;

    /** Value of a source on the newest note (or the shared value if nothing is sounding). */
    float liveSource (engine::ModSource s) const noexcept { return live[engine::index (s)]; }
    float liveLfoPhase (int lfo) const noexcept { return lfoPhase[(size_t) lfo]; }

    /** `dsp::Envelope::Stage` of the newest note's envelope: 0 idle, 1 attack, 2 decay, 3 sustain, 4 release. */
    int liveEnvStage (bool amp) const noexcept { return envStage[amp ? 0 : 1]; }

    //==========================================================================
    // Destinations.
    void registerTarget (ModTarget*);
    void unregisterTarget (ModTarget*);
    const std::vector<ModTarget*>& getTargets() const noexcept { return targets; }

    /** The visible target whose knob is under `screenPoint`, if any. */
    ModTarget* findTargetAt (juce::Point<int> screenPoint) const;

    /** The arcs for a control that shows `dests`, around a value of `base` (its own units).
        `toNormalised` maps a value in those units to the control's 0..1 travel.
        With `hardWiredFilterEnvAmount` the filter envelope's amount (a fraction) is added as an
        arc that is not a slot. */
    std::vector<ModArc> arcsFor (std::span<const engine::ModDest> dests, float base,
                                 const std::function<float (float)>& toNormalised,
                                 std::optional<float> hardWiredFilterEnvAmount = std::nullopt) const;

    //==========================================================================
    // Making a connection: drag a handle onto a knob, or click a handle and then a knob.
    enum class Mode { idle, dragging, armed };

    Mode getMode() const noexcept { return mode; }
    engine::ModSource getConnectingSource() const noexcept { return connectingSource; }
    bool isConnecting() const noexcept { return mode != Mode::idle; }
    juce::Point<int> getPointer() const noexcept { return pointer; }             // screen coordinates
    juce::Point<int> getHandleCentre() const noexcept { return handleCentre; }   // screen coordinates

    void beginDrag (engine::ModSource, juce::Point<int> handleCentreOnScreen);
    void movePointer (juce::Point<int> screenPoint);
    void arm (engine::ModSource, juce::Point<int> handleCentreOnScreen);
    void cancelConnecting();

    /** Is `target` somewhere `source` can go right now? */
    bool isValidTarget (const ModTarget&, engine::ModSource) const;

    /** Connects the connecting source to `target`, asking which destination if there is more
        than one, then ends the connection. */
    void dropOn (ModTarget&, juce::Component* menuParent);

    //==========================================================================
    /** A one-line message ("All 8 slots are in use") shown for a few seconds. */
    void showStatus (const juce::String&);
    const juce::String& getStatus() const noexcept { return status; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();
    void connect (engine::ModSource, engine::ModDest);

    /** Both ends are in the patch, and it is not an LFO moving its own rate. */
    bool canDrive (engine::ModSource, engine::ModDest) const;

    plugin::ScratchpadProcessor& processor;
    juce::ListenerList<Listener> listeners;

    engine::ModConnections connections {};
    engine::ModSourceValues live {};
    std::array<float, 2> lfoPhase {};
    std::array<int, 2> envStage {};
    std::vector<ModTarget*> targets;

    Mode mode = Mode::idle;
    engine::ModSource connectingSource = engine::ModSource::none;
    juce::Point<int> pointer, handleCentre;

    std::shared_ptr<bool> aliveFlag = std::make_shared<bool> (true); // for menus that outlive a drop
    juce::String status;
    int statusTicks = 0;
    bool active = false;
};

/** Something a connection can end on: a knob, or a static dial. */
class ModTarget
{
public:
    virtual ~ModTarget() = default;

    /** The destinations this control stands for. The first is the default one; with more than one,
        a drop asks which. */
    virtual std::span<const engine::ModDest> getDestinations() const = 0;

    virtual juce::Component& getComponent() = 0;

    /** The disc and its arcs, in `getComponent()` coordinates. */
    virtual juce::Point<float> getRingCentre() const = 0;
    virtual float getDiscRadius() const = 0;
    virtual std::vector<ModArc> getArcs() const = 0;

    static constexpr float firstArcRadius = 20.5f;
    static constexpr float arcSpacing = 3.0f;
};

} // namespace scratchpad::ui
