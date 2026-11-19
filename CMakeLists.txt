cmake_minimum_required(VERSION 3.22)

project(OpenMix VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives in external/JUCE (pinned to 9.0.5). If it's missing, fetch it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    add_subdirectory(external/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_gui_app(OpenMix
    PRODUCT_NAME "OpenMix"
    COMPANY_NAME "OpenMix"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

juce_generate_juce_header(OpenMix)

target_sources(OpenMix PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MixerEngine.cpp
    Source/ChannelStrip.cpp
    Source/VoicePad.cpp
    Source/Session.cpp
    Source/ExternalIO.cpp
    Source/AppCapture.cpp
    Source/OpenMixLookAndFeel.cpp)

target_compile_definitions(OpenMix PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=1                 # uses JUCE's bundled ASIO headers
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_VERSION>")

# Core unit tests (plain C++, no JUCE). Run with: ctest --test-dir build -C Release
option(OPENMIX_BUILD_TESTS "Build the core unit tests" ON)
if(OPENMIX_BUILD_TESTS)
    enable_testing()
    add_executable(OpenMixCoreTests tests/CoreTests.cpp)
    target_include_directories(OpenMixCoreTests PRIVATE Source)
    add_test(NAME OpenMixCoreTests COMMAND OpenMixCoreTests)
endif()

# Deliberately no /fp:fast (it lets the compiler assume NaN/Inf never happen, which breaks the
# safety checks) and no /arch:AVX2 (the released .exe must run on any x64 CPU).

target_link_libraries(OpenMix
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        mmdevapi            # ActivateAudioInterfaceAsync (per-app capture)
        avrt                # MMCSS thread priority for capture threads
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
