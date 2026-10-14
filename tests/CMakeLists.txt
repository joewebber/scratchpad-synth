file(GLOB SCRATCHPAD_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
add_executable(ScratchpadTests ${SCRATCHPAD_TEST_SOURCES})

target_link_libraries(ScratchpadTests
    PRIVATE
        ScratchpadCore
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags)

include(Catch)
catch_discover_tests(ScratchpadTests)
