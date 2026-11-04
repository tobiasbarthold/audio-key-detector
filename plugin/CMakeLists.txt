# Key Detector: VST3/AU plugin, standalone app and a command line check tool.
#
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin
#
# Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0135 NEW)   # URL downloads: extraction time stamps

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(KeyDetector VERSION 0.2.0 LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# JUCE 8 (AGPLv3 / commercial dual licence), pinned release.
FetchContent_Declare(juce
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.17-security.tar.gz
    URL_HASH SHA256=d37bab9f2330c50f42ba979575f6b55d5d3b23fa94221c2e57cac3abb69ac6d9)
FetchContent_MakeAvailable(juce)

include(cmake/Essentia.cmake)

# JUCE-free analysis core (Essentia).
add_library(kd_engine STATIC Source/Engine/KeyEngine.cpp)
target_include_directories(kd_engine PUBLIC Source/Engine)
target_link_libraries(kd_engine PUBLIC essentia_key)
target_compile_options(kd_engine PRIVATE -Wall -Wextra)

set(KD_SHARED_SOURCES Source/Engine/AudioPrep.cpp)
set(KD_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=0)      # mp3/m4a are decoded by macOS (CoreAudioFormat)

# ---------------------------------------------------------------- command line check tool
juce_add_console_app(KeyDetectCli PRODUCT_NAME "keydetect-cpp")
target_sources(KeyDetectCli PRIVATE Source/Cli/Main.cpp ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetectCli PRIVATE ${KD_COMMON_DEFINITIONS})
target_link_libraries(KeyDetectCli PRIVATE
    kd_engine
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- plugin (VST3 + AU)
juce_add_plugin(KeyDetector
    PRODUCT_NAME "Key Detector"
    COMPANY_NAME "Tobias Barthold"
    COMPANY_WEBSITE "https://github.com/tobiasbarthold/audio-key-detector"
    BUNDLE_ID com.tobiasbarthold.keydetector
    PLUGIN_MANUFACTURER_CODE Tbkd
    PLUGIN_CODE Kdet
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Analyzer
    DESCRIPTION "Shows the musical key of the audio on its track"
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(KeyDetector PRIVATE
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/LiveAnalyzer.cpp
    ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetector PUBLIC ${KD_COMMON_DEFINITIONS} JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(KeyDetector PRIVATE
    kd_engine
    juce::juce_audio_utils
    juce::juce_audio_formats
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- standalone app
juce_add_gui_app(KeyDetectorApp
    PRODUCT_NAME "Key Detector"
    COMPANY_NAME "Tobias Barthold"
    BUNDLE_ID com.tobiasbarthold.keydetector.app
    DOCUMENT_EXTENSIONS wav aif aiff flac mp3 m4a ogg caf aac)
target_sources(KeyDetectorApp PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetectorApp PRIVATE
    ${KD_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:KeyDetectorApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:KeyDetectorApp,JUCE_VERSION>")
target_link_libraries(KeyDetectorApp PRIVATE
    kd_engine
    juce::juce_gui_extra
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- live analysis test (not installed)
juce_add_console_app(KeyDetectorLiveTest PRODUCT_NAME "KeyDetectorLiveTest")
target_sources(KeyDetectorLiveTest PRIVATE tests/live_test.cpp Source/Plugin/LiveAnalyzer.cpp ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetectorLiveTest PRIVATE ${KD_COMMON_DEFINITIONS})
target_link_libraries(KeyDetectorLiveTest PRIVATE
    kd_engine
    juce::juce_audio_formats
    juce::juce_events
    juce::juce_recommended_config_flags)

# ---------------------------------------------------------------- UI snapshots (not installed)
juce_add_console_app(KeyDetectorSnapshots PRODUCT_NAME "KeyDetectorSnapshots")
target_sources(KeyDetectorSnapshots PRIVATE
    tests/snapshot.cpp
    Source/App/MainComponent.cpp
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.cpp
    Source/Plugin/LiveAnalyzer.cpp
    ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetectorSnapshots PRIVATE
    ${KD_COMMON_DEFINITIONS} JucePlugin_Name="Key Detector" JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(KeyDetectorSnapshots PRIVATE
    kd_engine
    juce::juce_gui_extra
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)

# ---------------------------------------------------------------- host test of the installed plugins
juce_add_console_app(KeyDetectorHostTest PRODUCT_NAME "KeyDetectorHostTest")
target_sources(KeyDetectorHostTest PRIVATE tests/host_test.cpp ${KD_SHARED_SOURCES})
target_compile_definitions(KeyDetectorHostTest PRIVATE
    ${KD_COMMON_DEFINITIONS} JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
target_link_libraries(KeyDetectorHostTest PRIVATE
    kd_engine
    "-framework CoreAudioKit"
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
