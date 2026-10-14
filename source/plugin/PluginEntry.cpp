// The only translation unit compiled into the plugin target itself. It owns
// the format-specific glue so the rest of the code stays in ScratchpadCore.

#include "plugin/PluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto* processor = new scratchpad::plugin::ScratchpadProcessor();

    // In the standalone app the window shows a route to the audio settings.
    // Plugin formats leave these empty and the UI hides the controls.
    if (processor->wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        processor->openAudioSettings = []
        {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        };

        processor->audioDeviceMissing = []
        {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            return holder != nullptr && holder->deviceManager.getCurrentAudioDevice() == nullptr;
        };
    }

    return processor;
}
