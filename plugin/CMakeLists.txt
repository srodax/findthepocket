include(FetchContent)

FetchContent_Declare(
  juce
  URL https://github.com/juce-framework/JUCE/archive/refs/heads/master.tar.gz
)

FetchContent_MakeAvailable(juce)

juce_add_plugin(FindThePocket
  COMPANY_NAME "FindThePocket"
  BUNDLE_ID com.findthepocket.plugin
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  PLUGIN_MANUFACTURER_CODE FtPk
  PLUGIN_CODE FtP1
  FORMATS VST3
  PRODUCT_NAME "Find The Pocket"
)

target_sources(FindThePocket
  PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    src/core/CalibrationEngine.cpp
    src/core/CalibrationEngine.h
    src/core/ScoringEngine.cpp
    src/core/ScoringEngine.h
    src/core/SessionEngine.cpp
    src/core/SessionEngine.h
    src/dsp/OnsetDetector.cpp
    src/dsp/OnsetDetector.h
    src/host/TransportSync.cpp
    src/host/TransportSync.h
)

target_compile_definitions(FindThePocket
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FindThePocket
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

option(FTP_BUILD_TESTS "Build Find The Pocket tests" ON)
if(FTP_BUILD_TESTS)
  enable_testing()
  add_executable(findthepocket_tests
    tests/ScoringEngineTests.cpp
    tests/CalibrationEngineTests.cpp
    src/core/CalibrationEngine.cpp
    src/core/CalibrationEngine.h
    src/core/ScoringEngine.cpp
    src/core/ScoringEngine.h
    src/core/SessionEngine.cpp
    src/core/SessionEngine.h
  )
  target_include_directories(findthepocket_tests PRIVATE src)
  target_compile_features(findthepocket_tests PRIVATE cxx_std_20)
  add_test(NAME findthepocket_tests COMMAND findthepocket_tests)
endif()
