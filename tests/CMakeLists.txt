add_executable(ScratchpadTests
    TestMain.cpp
    TestUtils.h
    OscillatorTests.cpp
    GateTests.cpp
    VoiceAllocatorTests.cpp
    EngineTests.cpp
    StateTests.cpp)

target_link_libraries(ScratchpadTests
    PRIVATE
        ScratchpadCore
        Catch2::Catch2WithMain
        juce::juce_recommended_config_flags)

include(Catch)
catch_discover_tests(ScratchpadTests)
