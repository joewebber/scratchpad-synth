#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Parameters.h"
#include "ui/ChoiceSegments.h"
#include "ui/Knob.h"
#include "ui/ModulationModel.h"
#include "ui/ModulationStrip.h"
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>


using namespace scratchpad;

namespace
{
void collect (juce::Component& component, std::vector<juce::Component*>& result)
{
    result.push_back (&component);

    for (auto* child : component.getChildren())
        collect (*child, result);
}

template <typename T>
T* findFirst (juce::Component& root)
{
    std::vector<juce::Component*> all;
    collect (root, all);

    for (auto* component : all)
        if (auto* match = dynamic_cast<T*> (component))
            return match;

    return nullptr;
}

template <typename T>
std::vector<T*> findAll (juce::Component& root)
{
    std::vector<juce::Component*> all;
    collect (root, all);
    std::vector<T*> matches;

    for (auto* component : all)
        if (auto* match = dynamic_cast<T*> (component))
            matches.push_back (match);

    return matches;
}
struct DesktopHost
{
    explicit DesktopHost (juce::Component& child, int width, int height)
    {
        root.setSize (width, height);
        root.addAndMakeVisible (child);
        root.addToDesktop (0);
        root.setVisible (true);
    }

    ~DesktopHost() { root.removeChildComponent (root.getChildComponent (0)); }

    juce::Component root;
};

}

TEST_CASE ("Keyboard sliders expose a range and operate without a mouse", "[a11y][keyboard]")
{
    plugin::ScratchpadProcessor processor;
    ui::Knob knob (processor.apvts, params::filterCutoff, "Cutoff", processor.undoManager, "Filter cutoff");
    knob.setBounds (0, 0, ui::Knob::knobWidth, ui::Knob::knobHeight);
    DesktopHost host (knob, ui::Knob::knobWidth, ui::Knob::knobHeight);

    auto& slider = knob.getSlider();
    const auto before = slider.getValue();
    auto* handler = slider.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getRole() == juce::AccessibilityRole::slider);
    REQUIRE (handler->getTitle() == "Filter cutoff");
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getRange().isValid());
    CHECK (handler->getValueInterface()->getCurrentValueAsString().isNotEmpty());

    REQUIRE (slider.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (slider.getValue() > before);
    REQUIRE (slider.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (slider.getValue() == slider.getMinimum());
    REQUIRE (slider.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (slider.getValue() == slider.getMaximum());
    REQUIRE (slider.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (slider.getValue() == Catch::Approx (slider.getDoubleClickReturnValue()));
}

TEST_CASE ("Choice selectors expose their current text and respond to arrows", "[a11y][keyboard]")
{
    plugin::ScratchpadProcessor processor;
    ui::ChoiceSegments selector (processor.apvts, params::osc1Wave, "Oscillator 1 wave", processor.undoManager,
                                 &ui::Palette::src);
    selector.setBounds (0, 0, 180, 24);
    DesktopHost host (selector, 180, 24);

    auto* handler = selector.getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getRole() == juce::AccessibilityRole::comboBox);
    REQUIRE (handler->getValueInterface() != nullptr);
    const auto before = selector.getSelected();
    REQUIRE (selector.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (selector.getSelected() == before + 1);
    CHECK (handler->getValueInterface()->getCurrentValueAsString().isNotEmpty());

    REQUIRE (selector.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (selector.getSelected() == 0);
    REQUIRE (selector.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (selector.getSelected() == 6);
}

TEST_CASE ("Connection rows adjust and remove a modulation from the keyboard", "[a11y][keyboard][modulation]")
{
    plugin::ScratchpadProcessor processor;
    processor.addModule (plugin::Module::filter);
    REQUIRE (processor.connectMod (engine::ModSource::velocity, engine::ModDest::cutoff)
             == plugin::ScratchpadProcessor::ConnectResult::created);

    ui::ModulationModel model (processor);
    ui::ConnectionsPanel panel (model);
    panel.setBounds (0, 0, ui::ConnectionsPanel::width, ui::ConnectionsPanel::height);
    DesktopHost host (panel, ui::ConnectionsPanel::width, ui::ConnectionsPanel::height);

    auto* row = findFirst<ui::ConnectionRow> (panel);
    REQUIRE (row != nullptr);
    row->refresh();

    auto* handler = row->getAccessibilityHandler();
    REQUIRE (handler != nullptr);
    REQUIRE (handler->getRole() == juce::AccessibilityRole::slider);
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getRange().getMinimumValue() == -100.0);
    CHECK (handler->getValueInterface()->getRange().getMaximumValue() == 100.0);

    const auto start = processor.getModConnections()[0].amount;
    REQUIRE (row->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (processor.getModConnections()[0].amount > start);
    REQUIRE (row->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (processor.getModConnections()[0].amount == Catch::Approx (0.5f));
    REQUIRE (row->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK_FALSE (processor.getModConnections()[0].active());
}

TEST_CASE ("The full editor gives interactive controls names and usable accessibility handlers", "[a11y][editor]")
{
    plugin::ScratchpadProcessor processor;
    processor.addModule (plugin::Module::filter);
    processor.addModule (plugin::Module::lfo1);
    plugin::ScratchpadEditor editor (processor);
    editor.addToDesktop (0);
    editor.setVisible (true);

    const auto buttons = findAll<juce::Button> (editor);
    REQUIRE_FALSE (buttons.empty());
    for (auto* button : buttons)
    {
        if (! button->isAccessible())
            continue;
        CHECK (button->getTitle().isNotEmpty());
        auto* handler = button->getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        CHECK (handler->getTitle().isNotEmpty());
    }

    const auto sliders = findAll<juce::Slider> (editor);
    REQUIRE_FALSE (sliders.empty());
    for (auto* slider : sliders)
    {
        INFO (slider->getTitle().toStdString());
        CHECK (slider->getTitle().isNotEmpty());
        auto* handler = slider->getAccessibilityHandler();
        REQUIRE (handler != nullptr);
        CHECK (handler->getValueInterface() != nullptr);
        CHECK (handler->getValueInterface()->getRange().isValid());
        CHECK (handler->getValueInterface()->getCurrentValueAsString().isNotEmpty());
    }
}
