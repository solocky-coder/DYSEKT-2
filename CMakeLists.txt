cmake_minimum_required(VERSION 3.22)
project(Dysekt VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/JUCE JUCE)

include(FetchContent)

# --- chowdsp_utils (SVF EQ filters — used by VoicePool per-slice EQ) ---
# Use source-only population (same pattern as Tracktion) to avoid configuring
# chowdsp's root CMakeLists.txt, which fails on Windows with FetchContent_MakeAvailable.
FetchContent_Declare(
    chowdsp_utils
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_utils
    GIT_TAG        v2.3.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)

FetchContent_GetProperties(chowdsp_utils)
if(NOT chowdsp_utils_POPULATED)
    FetchContent_Populate(chowdsp_utils)
endif()

# Register only the module(s) we actually use — bypasses the entire chowdsp build system.
juce_add_modules("${chowdsp_utils_SOURCE_DIR}/modules/chowdsp_filters")
message(STATUS "chowdsp_utils fetched; chowdsp_filters registered via juce_add_modules")

# --- Tracktion Engine (Source Download Only) ---
# Populates the source tree without evaluating Tracktion's own root CMake tree.
# This completely bypasses its internal JUCE copies, benchmarks, and tests.
FetchContent_Declare(
    tracktion_engine
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine
    GIT_TAG        master
    GIT_SUBMODULES ""
)

FetchContent_GetProperties(tracktion_engine)
if(NOT tracktion_engine_POPULATED)
    FetchContent_Populate(tracktion_engine)
endif()

# Register Tracktion modules natively into the JUCE build ecosystem
juce_add_modules("${tracktion_engine_SOURCE_DIR}/modules/tracktion_engine")
juce_add_modules("${tracktion_engine_SOURCE_DIR}/modules/tracktion_graph")

set(TRACKTION_ENGINE_MODULES_DIR "${tracktion_engine_SOURCE_DIR}/modules")
message(STATUS "Tracktion Engine fetched and modules registered cleanly via juce_add_module")

# --- Ableton Link (tempo sync across apps on LAN/WiFi) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/link/include/ableton/Link.hpp")
    set(DYSEKT_HAS_LINK TRUE)
    message(STATUS "Ableton Link SDK found — Link sync enabled")
else()
    set(DYSEKT_HAS_LINK FALSE)
    message(STATUS "Ableton Link SDK NOT found — Link sync disabled.")
    message(STATUS "  To enable: git submodule add https://github.com/Ableton/link link")
endif()

# --- sfizz (SFZ/SF2 engine) ---
if(TRUE)
    set(SFIZZ_CLIENTS    OFF CACHE BOOL "" FORCE)
    set(SFIZZ_SHARED     OFF CACHE BOOL "" FORCE)
    set(SFIZZ_JACK       OFF CACHE BOOL "" FORCE)
    set(SFIZZ_LV2        OFF CACHE BOOL "" FORCE)
    set(SFIZZ_VST        OFF CACHE BOOL "" FORCE)
    set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(SFIZZ_TESTS      OFF CACHE BOOL "" FORCE)
    set(SFIZZ_RENDER     OFF CACHE BOOL "" FORCE)
    add_subdirectory(sfizz EXCLUDE_FROM_ALL)
    set(DYSEKT_HAS_SFIZZ TRUE)
    message(STATUS "sfizz found — SF2/SFZ slice-render support enabled")
else()
    set(DYSEKT_HAS_SFIZZ FALSE)
    message(STATUS "sfizz NOT found at ./sfizz — SF2/SFZ slice-render support disabled.")
endif()

# --- FluidSynth (SF2 live-playback engine) ---
find_package(FluidSynth CONFIG QUIET)

if(FluidSynth_FOUND)
    set(DYSEKT_HAS_FLUIDSYNTH TRUE)
    message(STATUS "FluidSynth ${FluidSynth_VERSION} found via find_package — SF2 live playback enabled")
else()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/fluidsynth/CMakeLists.txt")
        set(FLUIDSYNTH_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
        set(FLUIDSYNTH_BUILD_DOCS      OFF CACHE BOOL "" FORCE)
        set(enable-dbus                OFF CACHE BOOL "" FORCE)
        set(enable-jack                OFF CACHE BOOL "" FORCE)
        set(enable-pulseaudio          OFF CACHE BOOL "" FORCE)
        set(enable-alsa                OFF CACHE BOOL "" FORCE)
        set(enable-coreaudio           OFF CACHE BOOL "" FORCE)
        set(enable-portaudio           OFF CACHE BOOL "" FORCE)
        set(enable-openmp              OFF CACHE BOOL "" FORCE)
        add_subdirectory(fluidsynth EXCLUDE_FROM_ALL)
        set(DYSEKT_HAS_FLUIDSYNTH TRUE)
        message(STATUS "FluidSynth found at ./fluidsynth — building from source")
    else()
        set(DYSEKT_HAS_FLUIDSYNTH FALSE)
        message(WARNING
            "FluidSynth NOT found — SF2 live playback will be disabled.\n"
            "  Install via vcpkg / homebrew / apt, or clone FluidSynth into ./fluidsynth.")
    endif()
endif()

if(DYSEKT_HAS_SFIZZ)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sfizz/src/sfizz.h")
        set(SFIZZ_C_API_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/sfizz/src")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sfizz/src/sfizz/sfizz.h")
        set(SFIZZ_C_API_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/sfizz/src/sfizz")
    else()
        message(WARNING "sfizz.h not found — SoundFontLoader/SfzPlayer may fail to compile")
        set(SFIZZ_C_API_INCLUDE "")
    endif()
    message(STATUS "sfizz C API include: ${SFIZZ_C_API_INCLUDE}")
endif()

# =============================================================================
#  VST3 Plugin target
# =============================================================================
juce_add_plugin(Dysekt
    COMPANY_NAME "DunSoft"
    PLUGIN_MANUFACTURER_CODE Dnst
    PLUGIN_CODE Dysk
    FORMATS VST3
    PRODUCT_NAME "DYSEKT"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

if (MSVC)
    target_compile_options(Dysekt PRIVATE /source-charset:utf-8)
endif()

juce_add_binary_data(DysektFonts SOURCES
    docs/DYSEKT_User_Manual.pdf
    src/ui/fonts/BarlowCondensed-Regular.ttf
    src/ui/fonts/BarlowCondensed-SemiBold.ttf
    src/ui/fonts/JetBrainsMono-Regular.ttf
    src/ui/fonts/JetBrainsMono-Bold.ttf
    src/ui/assets/dysekt_logo.svg
    src/ui/assets/Empty.wav
)

target_sources(Dysekt PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/audio/SampleData.cpp
    src/audio/SliceManager.cpp
    src/audio/VoicePool.cpp
    src/audio/GrainEngine.cpp
    src/audio/LazyChopEngine.cpp
    src/audio/SoundFontLoader.cpp
    src/audio/SfzPlayer.cpp
    src/params/ParamLayout.cpp
    src/ui/DysektLookAndFeel.cpp
    src/ui/HeaderBar.cpp
    src/ui/SliceControlBar.cpp
    src/ui/WaveformView.cpp
    src/ui/WaveformCache.cpp
    src/ui/SliceLane.cpp
    src/ui/ActionPanel.cpp
    src/ui/LogoBar.cpp
    src/ui/KeysPanel.cpp
    src/ui/FileBrowserPanel.cpp
    src/ui/SliceLcdDisplay.cpp
    src/ui/TrimDialog.cpp
    src/ui/MidiLearnDialog.cpp
    src/ui/MixerPanel.cpp
    src/ui/ShortcutsPanel.cpp
    src/ui/PadGridView.cpp
    src/ui/SliceWaveformLcd.cpp
    src/ui/DualLcdControlFrame.cpp
    src/ui/SfzDropdownPanel.cpp
    src/ui/SfzModulePanel.cpp
    src/ui/WaveformOverview.cpp
    src/ui/ArchiveIntegration.cpp
    src/ui/GlobalEqPanel.cpp
)

target_include_directories(Dysekt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${DYSEKT_HAS_LINK}>:${CMAKE_CURRENT_SOURCE_DIR}/link/include>
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-linear
    $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:${SFIZZ_C_API_INCLUDE}>
)

target_compile_definitions(Dysekt PUBLIC
    JUCE_USE_CURL=0
    DYSEKT_HAS_LINK=$<BOOL:${DYSEKT_HAS_LINK}>
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    DYSEKT_HAS_SFIZZ=$<BOOL:${DYSEKT_HAS_SFIZZ}>
    DYSEKT_HAS_FLUIDSYNTH=$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>
)

target_link_libraries(Dysekt
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        DysektFonts
        $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:sfizz::sfizz>
        $<$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>:FluidSynth::libfluidsynth>
        $<$<PLATFORM_ID:Linux>:asound>
        chowdsp_filters   # juce_add_modules target — no chowdsp:: namespace prefix
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# =============================================================================
#  DYSEKT Standalone App
# =============================================================================
juce_add_gui_app(DysektStandalone
    PRODUCT_NAME "DYSEKT"
)

if (MSVC)
    target_compile_options(DysektStandalone PRIVATE /source-charset:utf-8)
endif()

target_sources(DysektStandalone PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/audio/SampleData.cpp
    src/audio/SliceManager.cpp
    src/audio/VoicePool.cpp
    src/audio/GrainEngine.cpp
    src/audio/LazyChopEngine.cpp
    src/audio/SoundFontLoader.cpp
    src/audio/SfzPlayer.cpp
    src/params/ParamLayout.cpp
    src/ui/DysektLookAndFeel.cpp
    src/ui/HeaderBar.cpp
    src/ui/SliceControlBar.cpp
    src/ui/WaveformView.cpp
    src/ui/WaveformCache.cpp
    src/ui/SliceLane.cpp
    src/ui/ActionPanel.cpp
    src/ui/LogoBar.cpp
    src/ui/KeysPanel.cpp
    src/ui/FileBrowserPanel.cpp
    src/ui/SliceLcdDisplay.cpp
    src/ui/TrimDialog.cpp
    src/ui/MidiLearnDialog.cpp
    src/ui/MixerPanel.cpp
    src/ui/ShortcutsPanel.cpp
    src/ui/PadGridView.cpp
    src/ui/SliceWaveformLcd.cpp
    src/ui/DualLcdControlFrame.cpp
    src/ui/SfzDropdownPanel.cpp
    src/ui/SfzModulePanel.cpp
    src/ui/WaveformOverview.cpp
    src/ui/ArchiveIntegration.cpp
    src/ui/GlobalEqPanel.cpp
    src/standalone/Main.cpp
    src/standalone/MainWindow.cpp
)

target_include_directories(DysektStandalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${DYSEKT_HAS_LINK}>:${CMAKE_CURRENT_SOURCE_DIR}/link/include>
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-linear
    $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:${SFIZZ_C_API_INCLUDE}>
    ${TRACKTION_ENGINE_MODULES_DIR}
)

target_compile_definitions(DysektStandalone PUBLIC
    JUCE_USE_CURL=0
    DYSEKT_HAS_LINK=$<BOOL:${DYSEKT_HAS_LINK}>
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    DYSEKT_HAS_SFIZZ=$<BOOL:${DYSEKT_HAS_SFIZZ}>
    DYSEKT_HAS_FLUIDSYNTH=$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>
    DYSEKT_STANDALONE=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=0
)

target_link_libraries(DysektStandalone
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_dsp
        DysektFonts
        $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:sfizz::sfizz>
        $<$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>:FluidSynth::libfluidsynth>
        $<$<PLATFORM_ID:Linux>:asound>
        chowdsp_filters   # juce_add_modules target — no chowdsp:: namespace prefix
        tracktion_engine
        tracktion_graph
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
