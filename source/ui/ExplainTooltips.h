#pragma once

#include "content/Content.h"
#include "ui/ExplainPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace scratchpad::ui
{

/** The tooltip that appears when the pointer rests on a control: the control's name and its "What it does"
    line from the copy, so the first explanation shows up right at the pointer as well as in the Explain panel.
    Components without an `explainId` fall back to JUCE's own tooltips (the suggested-module reason). */
class ExplainTooltips final : public juce::TooltipWindow
{
public:
    explicit ExplainTooltips (juce::Component* parent)
        : juce::TooltipWindow (parent, 600)
    {
        setInterceptsMouseClicks (false, false);
    }

    juce::String getTipFor (juce::Component& c) override
    {
        const auto id = ExplainPanel::explainIdOf (&c);

        if (id.isNotEmpty())
        {
            const auto& content = content::Content::get();
            const auto* entry = content.param (id);

            if (entry == nullptr)
                entry = content.control (id);

            if (entry == nullptr)
                entry = content.module (id);

            if (entry != nullptr)
                return entry->title + ": " + entry->what;
        }

        return juce::TooltipWindow::getTipFor (c);
    }
};

} // namespace scratchpad::ui
