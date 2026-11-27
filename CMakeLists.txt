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
option(OPENMIX_ENABLE_DEEPFILTERNET "Build the DeepFilterNet 3 high-quality denoiser runtime (requires Cargo)" ON)
set(VST2_SDK_DIR "" CACHE PATH "Path to a licensed VST2 SDK (folder containing pluginterfaces/vst2.x/aeffect.h)")
if(OPENMIX_ENABLE_VST2)
    if(NOT VST2_SDK_DIR OR NOT EXISTS "${VST2_SDK_DIR}/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "OPENMIX_ENABLE_VST2=ON requires -DVST2_SDK_DIR=<path> pointing at a VST2 SDK containing pluginterfaces/vst2.x/aeffect.h (from vstsdk3610_11_06_2018_build_37 or older). The SDK is not included in this repo and Steinberg no longer issues new VST2 licences.")
    endif()
    set(OPENMIX_VST2_HOST 1)
else()
    set(OPENMIX_VST2_HOST 0)
endif()

# DeepFilterNet's Rust C-API is built as a small runtime DLL. Its model weights are
# intentionally kept out of the app and repository; users install the official model file.
if(OPENMIX_ENABLE_DEEPFILTERNET)
    find_program(OPENMIX_CARGO_EXECUTABLE cargo HINTS "$ENV{USERPROFILE}/.cargo/bin")
    if(NOT OPENMIX_CARGO_EXECUTABLE)
        message(FATAL_ERROR "OPENMIX_ENABLE_DEEPFILTERNET=ON requires Rust Cargo. Install Rust from https://rust-lang.org/install or configure with -DOPENMIX_ENABLE_DEEPFILTERNET=OFF.")
    endif()

    set(OPENMIX_DF_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/external/DeepFilterNet/libDF/Cargo.toml")
    set(OPENMIX_DF_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/DeepFilterNet")
    set(OPENMIX_DF_LIBRARY "${OPENMIX_DF_TARGET_DIR}/release/df.dll")
    file(GLOB_RECURSE OPENMIX_DF_RUST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/external/DeepFilterNet/libDF/src/*.rs")
    add_custom_command(OUTPUT "${OPENMIX_DF_LIBRARY}"
        COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${OPENMIX_DF_TARGET_DIR}"
            "${OPENMIX_CARGO_EXECUTABLE}" build --locked --release --no-default-features --features capi
            --manifest-path "${OPENMIX_DF_MANIFEST}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/external/DeepFilterNet/libDF"
        DEPENDS "${OPENMIX_DF_MANIFEST}"
            "${CMAKE_CURRENT_SOURCE_DIR}/external/DeepFilterNet/libDF/Cargo.lock"
            ${OPENMIX_DF_RUST_SOURCES}
        COMMENT "Building DeepFilterNet 3 runtime"
        VERBATIM)
    add_custom_target(OpenMixDeepFilterNetRuntime ALL DEPENDS "${OPENMIX_DF_LIBRARY}")
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

if(OPENMIX_ENABLE_DEEPFILTERNET)
    add_dependencies(OpenMix OpenMixDeepFilterNetRuntime)
    add_custom_command(TARGET OpenMix POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OPENMIX_DF_LIBRARY}" "$<TARGET_FILE_DIR:OpenMix>/df.dll"
        VERBATIM)
endif()

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
    Source/engine/PluginSandbox.cpp
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
    Source/ui/HotkeysPanel.cpp
    Source/ui/RoutingPanel.cpp)

target_include_directories(OpenMix PRIVATE Source)
if(OPENMIX_ENABLE_VST2)
    # JUCE's VST2 host code includes <pluginterfaces/vst2.x/aeffect.h> from here.
    target_include_directories(OpenMix PRIVATE ${VST2_SDK_DIR})
endif()

target_compile_definitions(OpenMix PRIVATE
    OPENMIX_DEEPFILTERNET_ENABLED=$<BOOL:${OPENMIX_ENABLE_DEEPFILTERNET}>
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
    JUCE_USE_MP3AUDIOFORMAT=1   # soundboard pads (Windows Media decoders are not used)
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:OpenMix,JUCE_VERSION>")
target_compile_options(OpenMix PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# Core unit tests (plain C++, no JUCE). Run with: ctest --test-dir build -C Release
option(OPENMIX_BUILD_TESTS "Build the unit tests" ON)
if(OPENMIX_BUILD_TESTS)
    enable_testing()
    add_executable(OpenMixCoreTests tests/CoreTests.cpp Source/dsp/Denoiser.cpp)
    target_include_directories(OpenMixCoreTests PRIVATE Source)
    target_compile_definitions(OpenMixCoreTests PRIVATE OPENMIX_DEEPFILTERNET_ENABLED=$<BOOL:${OPENMIX_ENABLE_DEEPFILTERNET}>)
    target_compile_options(OpenMixCoreTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    target_link_libraries(OpenMixCoreTests PRIVATE rnnoise)
    add_test(NAME OpenMixCoreTests COMMAND OpenMixCoreTests)

    add_executable(OpenMixAppTests
        tests/AppTests.cpp
        Source/app/AppSettings.cpp
        Source/app/GlobalHotkeys.cpp
        Source/app/Session.cpp
        Source/app/RemoteCommands.cpp
        Source/app/MidiControl.cpp
        Source/engine/MixerEngine.cpp
        Source/engine/ChannelStrip.cpp
        Source/engine/PluginChain.cpp
        Source/engine/PluginSandbox.cpp
        Source/engine/VoicePad.cpp
        Source/engine/ExternalIO.cpp
        Source/engine/AppCapture.cpp
        Source/engine/AppRouting.cpp
        Source/engine/PluginScanner.cpp
        Source/creator/Recorder.cpp
        Source/creator/Soundboard.cpp
        Source/dsp/Denoiser.cpp
        Source/ui/Widgets.cpp
        Source/ui/OpenMixLookAndFeel.cpp)
    target_include_directories(OpenMixAppTests PRIVATE Source $<TARGET_PROPERTY:OpenMix,BINARY_DIR>/OpenMix_artefacts/JuceLibraryCode)
    target_compile_options(OpenMixAppTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
    add_dependencies(OpenMixAppTests OpenMix)
    target_compile_definitions(OpenMixAppTests PRIVATE
        OPENMIX_DEEPFILTERNET_ENABLED=$<BOOL:${OPENMIX_ENABLE_DEEPFILTERNET}>
        OPENMIX_DISABLE_PLUGINCHAIN_SANDBOX=1
        OPENMIX_ENABLE_SANDBOX_TEST_HOOKS=1
        JUCE_MODAL_LOOPS_PERMITTED=1   # tests run the message loop to deliver async callbacks
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ASIO=1
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=${OPENMIX_VST2_HOST}
        OPENMIX_VST2_ENABLED=${OPENMIX_VST2_HOST}
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(OpenMixAppTests PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_osc
        rnnoise
        mmdevapi
        avrt
        runtimeobject)
    add_test(NAME OpenMixAppTests COMMAND OpenMixAppTests)
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
        juce::juce_cryptography    # pinned model archive verification
        rnnoise
        mmdevapi            # ActivateAudioInterfaceAsync (per-app capture)
        avrt                # MMCSS thread priority for capture threads
        runtimeobject       # RoGetActivationFactory (per-app output device)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
