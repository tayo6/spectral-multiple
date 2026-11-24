cmake_minimum_required(VERSION 3.22)

project(WaveLens VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static MSVC runtime: the plug-in has no dependency on the VC++ redistributable
# (important on Windows 7, which does not ship the Universal CRT by default).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE 7.0.12. Either point JUCE_PATH at a local checkout, or let CMake fetch it.
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE 7.0.12 checkout (optional)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        7.0.12
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(WaveLens
    COMPANY_NAME                "WaveLens Audio"
    BUNDLE_ID                   "audio.wavlensaudio.wavlens"
    PLUGIN_MANUFACTURER_CODE    Wlau
    PLUGIN_CODE                 Wlns
    FORMATS                     VST3
    PRODUCT_NAME                "WaveLens"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    VST3_CATEGORIES             Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(WaveLens PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/LayeredSampleEngine.cpp
    Source/LayeredSampleEngine.h
    Source/LayerEnvelopeComponent.cpp
    Source/LayerEnvelopeComponent.h
    Source/StateMigration.h)

target_compile_definitions(WaveLens PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    $<$<PLATFORM_ID:Windows>:WINVER=0x0601>
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0601>
    $<$<PLATFORM_ID:Windows>:NTDDI_VERSION=0x06010000>)

target_link_libraries(WaveLens
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

include(CTest)
option(BUILD_ENGINE_TESTS "Build standalone C++17 engine unit tests" ON)

if(BUILD_ENGINE_TESTS)
    add_executable(SpectralMultipleEngineTests
        Tests/EngineTests.cpp
        Source/LayeredSampleEngine.cpp
        Source/StateMigration.h)

    # Keep test shim strictly private to SpectralMultipleEngineTests so it never shadows real JUCE headers on WaveLens.
    target_include_directories(SpectralMultipleEngineTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests/juce_test_shim
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_compile_features(SpectralMultipleEngineTests PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(SpectralMultipleEngineTests PRIVATE /W4)
    else()
        target_compile_options(SpectralMultipleEngineTests PRIVATE -Wall -Wextra)
    endif()

    if(UNIX AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(SpectralMultipleEngineTests PRIVATE Threads::Threads)
    endif()

    add_test(NAME SpectralMultipleEngineTests COMMAND SpectralMultipleEngineTests)
endif()
