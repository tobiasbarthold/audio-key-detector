# Key Detector: VST3/AU plugin, standalone app and a command line check tool.
#
#   cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/plugin
#
# Copyright (C) 2026 Tobias Barthold. GNU Affero General Public License v3.0.

cmake_minimum_required(VERSION 3.22)

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
