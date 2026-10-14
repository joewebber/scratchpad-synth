cmake_minimum_required(VERSION 3.25)

project(ScratchpadSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

option(SCRATCHPAD_BUILD_TESTS "Build the Catch2 test suite" ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin target: Standalone + VST3 from one project.
# ---------------------------------------------------------------------------
juce_add_plugin(ScratchpadSynth
    COMPANY_NAME                "Scratchpad"
    PRODUCT_NAME                "Scratchpad Synth"
    BUNDLE_ID                   com.scratchpad.synth
    PLUGIN_MANUFACTURER_CODE    Scpd
    PLUGIN_CODE                 Scsy
    FORMATS                     Standalone VST3
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    VST3_CATEGORIES             Instrument Synth
    COPY_PLUGIN_AFTER_BUILD     FALSE)

# ---------------------------------------------------------------------------
# Core library: engine, DSP, state and UI. Everything except the plugin entry
# point lives here so the tests can link it without the plugin wrappers.
# ---------------------------------------------------------------------------
# Jost (SIL OFL, see assets/fonts/OFL.txt), embedded so the UI looks the same everywhere.
juce_add_binary_data(ScratchpadFonts
    HEADER_NAME FontData.h
    NAMESPACE   fontdata
    SOURCES
        assets/fonts/Jost-300-Light.ttf
        assets/fonts/Jost-400-Book.ttf
        assets/fonts/Jost-500-Medium.ttf)

add_library(ScratchpadCore STATIC
    source/dsp/Gate.h
    source/dsp/Oscillator.h
    source/engine/Voice.h
    source/engine/VoiceAllocator.h
    source/engine/VoiceAllocator.cpp
    source/engine/SynthEngine.h
    source/engine/SynthEngine.cpp
    source/plugin/Parameters.h
    source/plugin/Parameters.cpp
    source/plugin/PatchState.h
    source/plugin/PatchState.cpp
    source/plugin/UiEventQueue.h
    source/plugin/PluginProcessor.h
    source/plugin/PluginProcessor.cpp
    source/plugin/PluginEditor.h
    source/plugin/PluginEditor.cpp
    source/ui/Theme.h
    source/ui/LookAndFeel.h
    source/ui/LookAndFeel.cpp
    source/ui/Typography.h
    source/ui/Typography.cpp
    source/ui/Neumorph.h
    source/ui/Neumorph.cpp
    source/ui/IconButton.h
    source/ui/WaveGlyphs.h
    source/ui/WaveGlyphs.cpp
    source/ui/Knob.h
    source/ui/Meter.h
    source/ui/TopBar.h
    source/ui/TopBar.cpp
    source/ui/WavePicker.h
    source/ui/WavePicker.cpp
    source/ui/PatchView.h
    source/ui/PatchView.cpp
    source/ui/PerformanceBar.h
    source/ui/PerformanceBar.cpp)

target_include_directories(ScratchpadCore PUBLIC source)

target_compile_definitions(ScratchpadCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ScratchpadCore
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
    PRIVATE
        ScratchpadFonts
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_sources(ScratchpadSynth PRIVATE source/plugin/PluginEntry.cpp)

target_compile_definitions(ScratchpadSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ScratchpadSynth
    PRIVATE
        ScratchpadCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
# Prints the parameter list (docs/parameters.md is this output).
add_executable(ScratchpadParameterList tools/ParameterList.cpp)
target_link_libraries(ScratchpadParameterList PRIVATE ScratchpadCore juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SCRATCHPAD_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

    enable_testing()
    add_subdirectory(tests)
endif()
