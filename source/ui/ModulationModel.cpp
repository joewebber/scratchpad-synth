#include "ui/ModulationModel.h"

#include "plugin/ModulationInfo.h"
#include "plugin/Parameters.h"
#include "ui/Accessibility.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace scratchpad::ui
{

using engine::ModDest;
using engine::ModSource;

SourceRange sourceRange (ModSource s)
{
    const bool bipolar = s == ModSource::lfo1 || s == ModSource::lfo2 || s == ModSource::key;
    return bipolar ? SourceRange { -1.0f, 1.0f } : SourceRange { 0.0f, 1.0f };
}

float modulatedValue (ModDest d, float base, float sum)
{
    switch (d)
    {
        case ModDest::none:
            return base;

        case ModDest::pitchAll:
        case ModDest::pitch1:
        case ModDest::pitch2:
        case ModDest::pitch3:
            return base + sum * engine::modPitchSemitones * 100.0f; // cents

        case ModDest::pulseWidth1:
        case ModDest::pulseWidth2:
        case ModDest::pulseWidth3:
            return base + sum * engine::modPulseWidth * 100.0f; // percent

        case ModDest::level1:
        case ModDest::level2:
        case ModDest::level3:
        case ModDest::noiseLevel:
        {
            const float factor = engine::levelFactor (sum, engine::modOscLevelMaxFactor);
            return factor < 1.0e-3f ? -120.0f : base + 20.0f * std::log10 (factor); // dB
        }

        case ModDest::cutoff:
            return base * std::exp2 (sum * engine::modCutoffOctaves); // Hz

        case ModDest::resonance:
            return base + sum * engine::modResonance * 100.0f; // percent

        case ModDest::ampLevel:
            return base * engine::levelFactor (sum, engine::modAmpLevelMaxFactor); // percent

        case ModDest::pan:
            return base + sum * engine::modPan * 100.0f; // percent, -100 .. +100

        case ModDest::lfo1Rate:
        case ModDest::lfo2Rate:
            return base * std::exp2 (sum * engine::modLfoRateOctaves); // Hz
    }

    return base;
}

juce::String destinationLongName (ModDest d)
{
    const auto names = params::modDestNames();
    return juce::isPositiveAndBelow ((int) d, names.size()) ? names[(int) d] : juce::String();
}

juce::String describeArcs (const std::vector<ModArc>& arcs)
{
    juce::StringArray movers;

    for (const auto& arc : arcs)
    {
        const bool bipolar = sourceRange (arc.source).low < 0.0f;
        const auto size = juce::String (std::abs (juce::roundToInt (arc.amount * 100.0f))) + " percent";
        const auto name = plugin::modulation::sourceName (arc.source);

        movers.add (bipolar ? name + ", plus or minus " + size
                            : name + ", " + (arc.amount < 0.0f ? "down to minus " : "up to plus ") + size);
    }

    return movers.isEmpty() ? juce::String() : "Modulated by " + movers.joinIntoString ("; ") + ".";
}

//==============================================================================
ModulationModel::ModulationModel (plugin::ScratchpadProcessor& p)
    : processor (p)
{
    processor.getModulationBroadcaster().addChangeListener (this);
    connections = processor.getModConnections();
}

ModulationModel::~ModulationModel()
{
    *aliveFlag = false;
    processor.getModulationBroadcaster().removeChangeListener (this);
    stopLive();
}

int ModulationModel::countConnections() const noexcept
{
    return (int) std::count_if (connections.begin(), connections.end(), [] (const auto& c) { return c.active(); });
}

void ModulationModel::setActive (bool shouldRun)
{
    active = shouldRun;

    if (shouldRun)
    {
        refresh();
        startLive (30);
    }
    else
    {
        stopLive();
        cancelConnecting();
    }
}

void ModulationModel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // One connection made or taken away, by any route, is worth saying. Several at once (a patch opened) are not:
    // the patch has its own announcement.
    const auto before = connections;
    refresh();

    int changed = -1, numChanged = 0;

    for (size_t i = 0; i < connections.size(); ++i)
        if (before[i].source != connections[i].source || before[i].dest != connections[i].dest)
        {
            changed = (int) i;
            ++numChanged;
        }

    if (numChanged != 1)
        return;

    const auto& was = before[(size_t) changed];
    const auto& now = connections[(size_t) changed];

    if (now.active() && ! was.active())
        a11y::announce ("Connected " + plugin::modulation::sourceName (now.source) + " to " + destinationLongName (now.dest)
                        + " at " + a11y::spokenAmount (now.amount));
    else if (was.active() && ! now.active())
        a11y::announce ("Removed connection from " + plugin::modulation::sourceName (was.source) + " to " + destinationLongName (was.dest));
}

void ModulationModel::refresh()
{
    connections = processor.getModConnections();
    listeners.call (&Listener::modulationChanged);
}

void ModulationModel::liveTick()
{
    const auto& source = processor.getLiveModulation();

    for (size_t i = 0; i < live.size(); ++i)
        live[i] = source.source[i].load (std::memory_order_relaxed);

    for (size_t i = 0; i < lfoPhase.size(); ++i)
        lfoPhase[i] = source.lfoPhase[i].load (std::memory_order_relaxed);

    for (size_t i = 0; i < envStage.size(); ++i)
        envStage[i] = source.envStage[i].load (std::memory_order_relaxed);

    listeners.call (&Listener::liveUpdated);

    if (statusExpiresMs > 0.0 && juce::Time::getMillisecondCounterHiRes() >= statusExpiresMs)
    {
        statusExpiresMs = 0.0;
        status = {};
        listeners.call (&Listener::statusChanged);
    }
}

//==============================================================================
void ModulationModel::registerTarget (ModTarget* t)
{
    targets.push_back (t);
}

void ModulationModel::unregisterTarget (ModTarget* t)
{
    targets.erase (std::remove (targets.begin(), targets.end(), t), targets.end());
}

ModTarget* ModulationModel::findTargetAt (juce::Point<int> screenPoint) const
{
    for (auto* t : targets)
    {
        auto& c = t->getComponent();

        if (! c.isShowing())
            continue;

        const auto local = c.getLocalPoint (nullptr, screenPoint).toFloat();

        if (local.getDistanceFrom (t->getRingCentre()) <= t->getDiscRadius() + ModTarget::firstArcRadius - 14.0f)
            return t;
    }

    return nullptr;
}

std::vector<ModArc> ModulationModel::arcsFor (std::span<const ModDest> dests, float base,
                                              const std::function<float (float)>& toNormalised,
                                              std::optional<float> hardWiredFilterEnvAmount) const
{
    std::vector<ModArc> arcs;

    const auto position = [&] (ModDest d, float sum) { return std::clamp (toNormalised (modulatedValue (d, base, sum)), 0.0f, 1.0f); };

    const auto add = [&] (int slot, ModSource source, ModDest d, float amount)
    {
        const auto range = sourceRange (source);
        const float a = position (d, amount * range.low);
        const float b = position (d, amount * range.high);
        arcs.push_back ({ slot, source, amount, std::min (a, b), std::max (a, b), position (d, amount * liveSource (source)) });
    };

    if (hardWiredFilterEnvAmount.has_value() && processor.isModulePresent (plugin::Module::filterEnv))
        add (-1, ModSource::filterEnv, ModDest::cutoff, *hardWiredFilterEnvAmount);

    for (size_t slot = 0; slot < connections.size(); ++slot)
    {
        const auto& c = connections[slot];

        if (c.active() && std::find (dests.begin(), dests.end(), c.dest) != dests.end())
            add ((int) slot, c.source, c.dest, c.amount);
    }

    return arcs;
}

//==============================================================================
void ModulationModel::beginDrag (ModSource source, juce::Point<int> handle)
{
    mode = Mode::dragging;
    connectingSource = source;
    handleCentre = handle;
    pointer = handle;
    listeners.call (&Listener::connectingChanged);
}

void ModulationModel::movePointer (juce::Point<int> screenPoint)
{
    pointer = screenPoint;
    listeners.call (&Listener::connectingChanged);
}

void ModulationModel::arm (ModSource source, juce::Point<int> handle)
{
    mode = Mode::armed;
    connectingSource = source;
    handleCentre = handle;
    pointer = handle;
    listeners.call (&Listener::connectingChanged);
}

void ModulationModel::cancelConnecting()
{
    if (mode == Mode::idle)
        return;

    mode = Mode::idle;
    connectingSource = ModSource::none;
    listeners.call (&Listener::connectingChanged);
}

bool ModulationModel::canDrive (ModSource source, ModDest dest) const
{
    const bool ownRate = (source == engine::lfoSource (0) && dest == engine::lfoRateDest (0))
                      || (source == engine::lfoSource (1) && dest == engine::lfoRateDest (1));

    return ! ownRate && processor.isModSourcePresent (source) && processor.isModDestinationPresent (dest);
}

bool ModulationModel::isValidTarget (const ModTarget& target, ModSource source) const
{
    const auto dests = target.getDestinations();
    return std::any_of (dests.begin(), dests.end(), [&] (ModDest d) { return canDrive (source, d); });
}

std::vector<ModDest> ModulationModel::validDestinations (ModSource source) const
{
    std::vector<ModDest> result;

    // With only oscillator 1 in the patch, "this oscillator" and "all of them" are the same thing: offer the one.
    const bool onlyOsc1 = ! processor.isModulePresent (plugin::Module::osc2) && ! processor.isModulePresent (plugin::Module::osc3);

    for (int i = 1; i < engine::numModDests; ++i)
    {
        const auto d = (ModDest) i;

        if (canDrive (source, d) && ! (onlyOsc1 && d == ModDest::pitch1))
            result.push_back (d);
    }

    return result;
}

void ModulationModel::chooseDestination (ModSource source, juce::Component& menuTarget)
{
    const auto choices = validDestinations (source);
    const auto name = plugin::modulation::sourceName (source);

    juce::PopupMenu menu;
    menu.addSectionHeader (choices.empty() ? name + ": nothing to move yet" : name + " moves");

    for (size_t i = 0; i < choices.size(); ++i)
    {
        bool already = false;

        for (const auto& c : connections)
            already = already || (c.source == source && c.dest == choices[i]);

        menu.addItem ((int) i + 1, destinationLongName (choices[i]), true, already);
    }

    auto alive = aliveFlag;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuTarget),
                        [this, alive, source, choices] (int result)
                        {
                            if (*alive && result >= 1 && result <= (int) choices.size())
                                connect (source, choices[(size_t) result - 1]);
                        });
}

void ModulationModel::dropOn (ModTarget& target, juce::Component* menuParent)
{
    const auto source = connectingSource;
    cancelConnecting();

    std::vector<ModDest> choices;

    for (const auto d : target.getDestinations())
        if (canDrive (source, d))
            choices.push_back (d);

    if (choices.empty() || source == ModSource::none)
        return;

    // With only oscillator 1 in the patch, "this oscillator" and "all of them" are the same thing.
    const bool onlyOsc1 = ! processor.isModulePresent (plugin::Module::osc2) && ! processor.isModulePresent (plugin::Module::osc3);
    const bool offersAllPitch = std::find (choices.begin(), choices.end(), ModDest::pitchAll) != choices.end();

    if (choices.size() == 1 || (onlyOsc1 && offersAllPitch))
    {
        connect (source, choices.size() == 1 ? choices.front() : ModDest::pitchAll);
        return;
    }

    const auto describe = [] (ModDest d)
    {
        if (d == ModDest::pitchAll)
            return juce::String ("All oscillators");

        const auto name = plugin::modulation::destinationName (d); // "Osc 2 pitch"
        return name.upToFirstOccurrenceOf (" pitch", false, false).replace ("Osc", "Oscillator") + " only";
    };

    juce::PopupMenu menu;
    for (size_t i = 0; i < choices.size(); ++i)
        menu.addItem ((int) i + 1, describe (choices[i]));

    auto alive = aliveFlag;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuParent != nullptr ? menuParent : &target.getComponent()),
                        [this, alive, source, choices] (int result)
                        {
                            if (*alive && result >= 1 && result <= (int) choices.size())
                                connect (source, choices[(size_t) result - 1]);
                        });
}

void ModulationModel::connect (ModSource source, ModDest dest)
{
    using Result = plugin::ScratchpadProcessor::ConnectResult;
    namespace info = plugin::modulation;

    const auto from = info::sourceLabel (source);
    const auto to = info::destinationName (dest);

    switch (processor.connectMod (source, dest))
    {
        case Result::created:
            showStatus (from + " now moves " + to.toLowerCase() + ". Drag its arc, or the row below, to change how far.", false); // the new connection announces itself
            break;
        case Result::alreadyConnected:
            showStatus (from + " already moves " + to.toLowerCase() + ".");
            break;
        case Result::noFreeSlot:
            showStatus ("All " + juce::String (engine::numModSlots) + " connection slots are in use. Remove one to make another.");
            break;
        case Result::unavailable:
            showStatus ("Add that module first.");
            break;
    }
}

void ModulationModel::showStatus (const juce::String& text, bool announceIt)
{
    status = text;
    statusExpiresMs = juce::Time::getMillisecondCounterHiRes() + 6000.0;
    listeners.call (&Listener::statusChanged);

    if (announceIt)
        a11y::announce (text.upToFirstOccurrenceOf (". ", false, false));
}

} // namespace scratchpad::ui
