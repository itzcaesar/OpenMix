cmake_minimum_required(VERSION 3.22)

project(OpenMix VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VST2 hosting is opt-in only, and OFF by default.
# Why: JUCE 9 still ships the VST2 host code (juce_VSTPluginFormat, gated by
# JUCE_PLUGINHOST_VST), but it needs aeffect.h / aeffectx.h from the VST2 SDK,
# which JUCE no longer bundles. Steinberg stopped issuing new VST2 licences in
# October 2018, so a new developer cannot legally distribute a VST2 host.
# Only enable this if you already hold a VST2 licence / SDK: pass
#   cmake --preset vs2026 -DOPENMIX_ENABLE_VST2=ON -DVST2_SDK_DIR="C:/SDKs/vstsdk2.4"
# The SDK itself is never committed to this repo and is not fetched by CMake.
# CI keeps this OFF, so the default build needs no SDK.
# Checked here, before JUCE is fetched, so a missing SDK fails fast.
option(OPENMIX_ENABLE_VST2 "Enable VST2 plugin hosting (requires a licensed VST2 SDK supplied via VST2_SDK_DIR)" OFF)
set(VST2_SDK_DIR "" CACHE PATH "Path to a licensed VST2 SDK (folder containing pluginterfaces/vst2.x/aeffect.h)")
if(OPENMIX_ENABLE_VST2)
    if(NOT VST2_SDK_DIR OR NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "OPENMIX_ENABLE_VST2=ON requires -DVST2_SDK_DIR=<path> pointing at a VST2 SDK containing pluginterfaces/vst2.x/aeffect.h (from vstsdk3610_11_06_2018_build_37 or older). The SDK is not included in this repo and Steinberg no longer issues new VST2 licences.")
    endif()
    set(OPENMIX_VST2_HOST 1)
else()
    set(OPENMIX_VST2_HOST 0)
endif()

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

# RNNoise v0.1.1 (BSD-3-Clause), the noise-removal network. The submodule is used as-is except
# pitch.c and celt_lpc.c, which come from Source/third_party/rnnoise (VLAs replaced for MSVC).
set(RNNOISE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/rnnoise)
add_library(rnnoise STATIC
    ${RNNOISE_DIR}/src/denoise.c
    ${RNNOISE_DIR}/src/rnn.c
    ${RNNOISE_DIR}/src/rnn_data.c
    ${RNNOISE_DIR}/src/kiss_fft.c
    Source/third_party/rnnoise/pitch.c
    Source/third_party/rnnoise/celt_lpc.c)
target_include_directories(rnnoise PUBLIC ${RNNOISE_DIR}/include PRIVATE ${RNNOISE_DIR}/src)
target_compile_definitions(rnnoise PUBLIC RNNOISE_EXPORT= PRIVATE _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
set_target_properties(rnnoise PROPERTIES C_STANDARD 11)
if(MSVC)
    target_compile_options(rnnoise PRIVATE /W0)   # third-party code: don't flood the build log
endif()

juce_add_gui_app(OpenMix
    PRODUCT_NAME "OpenMix"
    COMPANY_NAME "OpenMix"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png"   # drawn by resources/make_icon.py
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

juce_generate_juce_header(OpenMix)

target_sources(OpenMix PRIVATE
    # app shell: window, settings, session, remote control
    Source/app/Main.cpp
    Source/app/MainComponent.cpp
    Source/app/AppSettings.cpp
    Source/app/Session.cpp
    Source/app/OscControl.cpp
    Source/app/RemoteCommands.cpp
    Source/app/RemoteServer.cpp
    Source/app/MidiControl.cpp
    Source/app/GlobalHotkeys.cpp

    # audio engine and device I/O
    Source/engine/MixerEngine.cpp
    Source/engine/ChannelStrip.cpp
    Source/engine/PluginChain.cpp
    Source/engine/VoicePad.cpp
    Source/engine/ExternalIO.cpp
    Source/engine/AppCapture.cpp
    Source/engine/AppRouting.cpp
    Source/engine/PluginScanner.cpp

    # creator tools
    Source/creator/Recorder.cpp
    Source/creator/Soundboard.cpp

    # DSP (the rest is header-only)
    Source/dsp/Denoiser.cpp

    # interface
    Source/ui/OpenMixLookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/StripView.cpp
    Source/ui/BusView.cpp
    Source/ui/Panels.cpp
    Source/ui/InsertRack.cpp
    Source/ui/SettingsPanel.cpp
    Source/ui/TrayIcon.cpp
    Source/ui/AnalyserPanel.cpp
    Source/ui/SoundboardPanel.cpp
    Source/ui/HotkeysPanel.cpp)

target_include_directories(OpenMix PRIVATE Source)
if(OPENMIX_ENABLE_VST2)
    # JUCE's VST2 host code includes <pluginterfaces/vst2.x/aeffect.h> from here.
    target_include_directories(OpenMix PRIVATE ${VST2_SDK_DIR})
endif()

target_compile_definitions(OpenMix PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=1                 # uses JUCE's bundled ASIO headers
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=${OPENMIX_VST2_HOST}
    OPENMIX_VST2_ENABLED=${OPENMIX_VST2_HOST}
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1   # soundboard pads (Windows Media decoders are not used)
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_VERSION>")

# Core unit tests (plain C++, no JUCE). Run with: ctest --test-dir build -C Release
option(OPENMIX_BUILD_TESTS "Build the core unit tests" ON)
if(OPENMIX_BUILD_TESTS)
    enable_testing()
    add_executable(OpenMixCoreTests tests/CoreTests.cpp Source/dsp/Denoiser.cpp)
    target_include_directories(OpenMixCoreTests PRIVATE Source)
    target_link_libraries(OpenMixCoreTests PRIVATE rnnoise)
    add_test(NAME OpenMixCoreTests COMMAND OpenMixCoreTests)
endif()

# Deliberately no /fp:fast (it lets the compiler assume NaN/Inf never happen, which breaks the
# safety checks) and no /arch:AVX2 (the released .exe must run on any x64 CPU).

target_link_libraries(OpenMix
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra        # tray icon
        juce::juce_dsp
        juce::juce_osc              # remote control
        rnnoise
        mmdevapi            # ActivateAudioInterfaceAsync (per-app capture)
        avrt                # MMCSS thread priority for capture threads
        runtimeobject       # RoGetActivationFactory (per-app output device)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
