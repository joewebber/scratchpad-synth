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
    VERSION                     ${PROJECT_VERSION}
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

# The copy (Explain panel text and glossary) as data, so wording can change without touching code.
juce_add_binary_data(ScratchpadContent
    HEADER_NAME ContentData.h
    NAMESPACE   contentdata
    SOURCES
        source/content/explanations.json
        source/content/glossary.json)

# Sources are globbed so adding a file needs no CMake edit; CONFIGURE_DEPENDS
# makes the build notice new files. The plugin entry point belongs to the
# plugin target only.
file(GLOB_RECURSE SCRATCHPAD_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)
list(REMOVE_ITEM SCRATCHPAD_CORE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/source/plugin/PluginEntry.cpp)

add_library(ScratchpadCore STATIC ${SCRATCHPAD_CORE_SOURCES})

target_include_directories(ScratchpadCore PUBLIC source)

target_compile_definitions(ScratchpadCore PUBLIC
    SCRATCHPAD_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ScratchpadCore
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
    PRIVATE
        ScratchpadFonts
        ScratchpadContent
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

target_sources(ScratchpadSynth PRIVATE source/plugin/PluginEntry.cpp)

target_compile_definitions(ScratchpadSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

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

