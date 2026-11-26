cmake_minimum_required(VERSION 3.22)

project(WaveLens VERSION 1.0.0 LANGUAGES C CXX)

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

# Additional sample decoders. Keep JUCE pinned at 7.0.12 so the existing
# Windows 7 deployment target is unchanged.
include(FetchContent)

function(wavelens_add_opusfile_dependencies)
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(INSTALL_CMAKE_PACKAGE_MODULE ON CACHE BOOL "" FORCE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE ON CACHE BOOL "" FORCE)
    set(OPUS_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(OP_DISABLE_HTTP ON CACHE BOOL "" FORCE)
    set(OP_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
    set(OP_DISABLE_DOCS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(wavelens_ogg GIT_REPOSITORY https://github.com/xiph/ogg.git GIT_TAG v1.3.6 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(wavelens_ogg)
    FetchContent_Declare(wavelens_opus GIT_REPOSITORY https://github.com/xiph/opus.git GIT_TAG v1.6.1 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(wavelens_opus)
    set(Ogg_DIR "${wavelens_ogg_BINARY_DIR}")
    set(Opus_DIR "${wavelens_opus_BINARY_DIR}")
    FetchContent_Declare(wavelens_opusfile
        GIT_REPOSITORY https://github.com/xiph/opusfile.git
        GIT_TAG        f92ff8b7f55a05b5205e26d9459441b97a557e39)
    FetchContent_MakeAvailable(wavelens_opusfile)
endfunction()

wavelens_add_opusfile_dependencies()

add_library(WaveLensAudioCodecDependencies INTERFACE)
target_link_libraries(WaveLensAudioCodecDependencies INTERFACE OpusFile::opusfile)

if(WIN32)
    FetchContent_Declare(wavelens_mole
        GIT_REPOSITORY https://github.com/perob/MOLE.git
        GIT_TAG f92ff8b7f55a05b5205e26d9459441b97a557e39)
    if(POLICY CMP0169)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_GetProperties(wavelens_mole)
    if(NOT wavelens_mole_POPULATED)
        FetchContent_Populate(wavelens_mole)
    endif()
    if(POLICY CMP0169)
        cmake_policy(POP)
    endif()
    juce_add_module("${wavelens_mole_SOURCE_DIR}/modules/mole_audio_formats")
    target_link_libraries(WaveLensAudioCodecDependencies INTERFACE mole_audio_formats)
    target_compile_definitions(WaveLensAudioCodecDependencies INTERFACE WAVELENS_ENABLE_MOLE_AUDIO_FORMATS=1)
else()
    target_compile_definitions(WaveLensAudioCodecDependencies INTERFACE WAVELENS_ENABLE_MOLE_AUDIO_FORMATS=0)
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
    Source/SpectralMultipleLookAndFeel.cpp
    Source/SpectralMultipleLookAndFeel.h
    Source/SnapbackTheme.h
    Source/PluginHeaderComponent.cpp
    Source/PluginHeaderComponent.h
    Source/MasterWaveformComponent.cpp
    Source/MasterWaveformComponent.h
    Source/WaveformControlBar.cpp
    Source/WaveformControlBar.h
    Source/FilterControlComponent.cpp
    Source/FilterControlComponent.h
    Source/SampleBrowserComponent.cpp
    Source/SampleBrowserComponent.h
    Source/SampleGroupComponent.cpp
    Source/SampleGroupComponent.h
    Source/SampleSelectorDot.h
    Source/MasterButton.h
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
        WaveLensAudioCodecDependencies
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
    target_compile_definitions(SpectralMultipleEngineTests PRIVATE
        WAVELENS_ENABLE_MOLE_AUDIO_FORMATS=0
        WAVELENS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")

    if(MSVC)
        target_compile_options(SpectralMultipleEngineTests PRIVATE /W4)
    else()
        target_compile_options(SpectralMultipleEngineTests PRIVATE -Wall -Wextra)
    endif()

    if(UNIX AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(SpectralMultipleEngineTests PRIVATE Threads::Threads)
    endif()

    target_link_libraries(SpectralMultipleEngineTests PRIVATE OpusFile::opusfile)
    add_test(NAME SpectralMultipleEngineTests COMMAND SpectralMultipleEngineTests)
endif()

option(BUILD_PROCESSOR_TESTS "Build JUCE WaveLensProcessor integration tests" ON)

if(BUILD_PROCESSOR_TESTS)
    juce_add_console_app(WaveLensProcessorTests
        PRODUCT_NAME "WaveLensProcessorTests")

    target_sources(WaveLensProcessorTests PRIVATE
        Tests/ProcessorTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/LayeredSampleEngine.cpp
        Source/LayeredSampleEngine.h
        Source/LayerEnvelopeComponent.cpp
        Source/LayerEnvelopeComponent.h
        Source/SpectralMultipleLookAndFeel.cpp
        Source/SpectralMultipleLookAndFeel.h
        Source/SnapbackTheme.h
        Source/PluginHeaderComponent.cpp
        Source/PluginHeaderComponent.h
        Source/MasterWaveformComponent.cpp
        Source/MasterWaveformComponent.h
        Source/WaveformControlBar.cpp
        Source/WaveformControlBar.h
        Source/FilterControlComponent.cpp
        Source/FilterControlComponent.h
        Source/SampleBrowserComponent.cpp
        Source/SampleBrowserComponent.h
        Source/SampleGroupComponent.cpp
        Source/SampleGroupComponent.h
        Source/SampleSelectorDot.h
        Source/MasterButton.h
        Source/StateMigration.h)

    target_include_directories(WaveLensProcessorTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_compile_features(WaveLensProcessorTests PRIVATE cxx_std_17)
    target_compile_definitions(WaveLensProcessorTests PRIVATE
        WAVELENS_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/fixtures")

    target_compile_definitions(WaveLensProcessorTests PRIVATE
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

    target_link_libraries(WaveLensProcessorTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            WaveLensAudioCodecDependencies
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME WaveLensProcessorTests COMMAND WaveLensProcessorTests)
endif()

option(BUILD_UI_SNAPSHOT "Build headless UI screenshot tool (renders the editor to PNG)" OFF)

if(BUILD_UI_SNAPSHOT)
    juce_add_console_app(UiSnapshot
        PRODUCT_NAME "UiSnapshot")

    target_sources(UiSnapshot PRIVATE
        Tools/UiSnapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/LayeredSampleEngine.cpp
        Source/LayeredSampleEngine.h
        Source/LayerEnvelopeComponent.cpp
        Source/LayerEnvelopeComponent.h
        Source/SpectralMultipleLookAndFeel.cpp
        Source/SpectralMultipleLookAndFeel.h
        Source/SnapbackTheme.h
        Source/PluginHeaderComponent.cpp
        Source/PluginHeaderComponent.h
        Source/MasterWaveformComponent.cpp
        Source/MasterWaveformComponent.h
        Source/WaveformControlBar.cpp
        Source/WaveformControlBar.h
        Source/FilterControlComponent.cpp
        Source/FilterControlComponent.h
        Source/SampleBrowserComponent.cpp
        Source/SampleBrowserComponent.h
        Source/SampleGroupComponent.cpp
        Source/SampleGroupComponent.h
        Source/SampleSelectorDot.h
        Source/MasterButton.h
        Source/StateMigration.h)

    target_include_directories(UiSnapshot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source)

    target_compile_features(UiSnapshot PRIVATE cxx_std_17)

    target_compile_definitions(UiSnapshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    target_link_libraries(UiSnapshot
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            WaveLensAudioCodecDependencies
            juce::juce_recommended_warning_flags)
endif()

