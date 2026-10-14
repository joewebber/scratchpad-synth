// Prints the parameter list as a Markdown table: ID, name, range, default and
// whether the host can automate it. docs/parameters.md is this output, and the
// design agent's copy file is checked against it.

#include "plugin/PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    scratchpad::plugin::ScratchpadProcessor processor;

    std::cout << "| ID | Name | Range | Default | Automatable |\n"
              << "| --- | --- | --- | --- | --- |\n";

    for (auto* parameter : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        if (ranged == nullptr)
            continue;

        const auto& range = ranged->getNormalisableRange();
        const auto text = [&] (float v) { return ranged->getText (ranged->convertTo0to1 (v), 0); };

        juce::String rangeText;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
            rangeText = choice->choices.joinIntoString (" / ");
        else if (dynamic_cast<juce::AudioParameterBool*> (ranged) != nullptr)
            rangeText = "off / on";
        else
            rangeText = text (range.start) + " to " + text (range.end);

        std::cout << "| `" << ranged->paramID << "` | " << ranged->getName (64) << " | " << rangeText << " | "
                  << ranged->getText (ranged->getDefaultValue(), 0) << " | "
                  << (ranged->isAutomatable() ? "yes" : "no") << " |\n";
    }

    return 0;
}
