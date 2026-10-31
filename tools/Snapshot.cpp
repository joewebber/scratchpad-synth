// Renders the editor to a PNG, for looking at the layout without a window.
//
//   ScratchpadSnapshot out.png [--light] [--empty] [--scale 1.0] [--explain] [--more "Osc 1"]
//
// By default the patch has every module present, so every card is visible. `--empty` is a new patch.
// `--more NAME` opens the More pop-over of the card whose More button is named "NAME more options".
// View settings are read from a throwaway folder, never the user's.

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
    juce::Button* findButton (juce::Component& root, const juce::String& title)
    {
        if (auto* b = dynamic_cast<juce::Button*> (&root); b != nullptr && b->getTitle() == title)
            return b;

        for (auto* child : root.getChildren())
            if (auto* found = findButton (*child, title))
                return found;

        return nullptr;
    }
}

int main (int argc, char** argv)
{
    using namespace scratchpad;

    juce::ScopedJuceInitialiser_GUI gui;

    juce::String out = "snapshot.png";
    bool light = false, empty = false, explain = false;
    juce::String more;
    double scale = 1.0;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);

        if (a == "--light")      light = true;
        else if (a == "--empty") empty = true;
        else if (a == "--explain") explain = true;
        else if (a == "--more" && i + 1 < argc)  more = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = juce::String (argv[++i]).getDoubleValue();
        else                     out = a;
    }

    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("scratchpad-snapshot");
    ui::ViewSettings::setDefaultFileOverride (folder.getChildFile ("view.settings"));

    int status = 1;

    {
        plugin::ScratchpadProcessor processor;

        if (! empty)
            for (const auto m : plugin::allModules)
                processor.addModule (m);

        plugin::ScratchpadEditor editor (processor);
        editor.setTheme (light ? ui::ViewSettings::Theme::light : ui::ViewSettings::Theme::dark);
        editor.setExplainCollapsed (! explain); // the panel starts folded
        const auto stageSize = editor.getBaseSize();
        editor.setSize (juce::roundToInt (stageSize.x * scale), juce::roundToInt (stageSize.y * scale));
        editor.addToDesktop (0);
        editor.setVisible (true);

        if (more.isNotEmpty())
        {
            if (auto* button = findButton (editor, more + " more options"))
            {
                button->onClick();
            }
            else
            {
                std::fprintf (stderr, "no More button for \"%s\"\n", more.toRawUTF8());
                return 1;
            }
        }

        // createComponentSnapshot paints synchronously.

        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        const juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (out));
        file.deleteFile();

        if (auto stream = file.createOutputStream())
        {
            juce::PNGImageFormat png;
            status = png.writeImageToStream (image, *stream) ? 0 : 1;
        }
    }

    ui::ViewSettings::setDefaultFileOverride ({});
    folder.deleteRecursively();
    return status;
}
