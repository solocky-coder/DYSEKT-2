cmake_minimum_required(VERSION 3.22)
project(Dysekt VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/JUCE JUCE)

# --- chowdsp_utils (SVF EQ filters — used by VoicePool per-slice EQ) ---
include(FetchContent)
FetchContent_Declare(
    chowdsp_utils
    GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_utils
    GIT_TAG        v2.3.0
    GIT_SHALLOW    TRUE
)
set(CHOWDSP_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(chowdsp_utils)

# --- tracktion_engine REMOVED ---
# tracktion_engine does not compile against JUCE 8.0.12
# (tracktion_PluginWindowState.cpp references juce::Displays::Display::userBounds
# which was removed in JUCE 7+). SequencerEngine.cpp now uses DYSEKT-SF's own
# tick scheduler exclusively. MidiClip.cpp keeps the te::MidiList* hook as a
# forward-declared pointer but all midiList calls are no-ops until tracktion
# is re-enabled against a compatible JUCE version.

# --- Ableton Link (tempo sync across apps on LAN/WiFi) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/link/include/ableton/Link.hpp")
    set(DYSEKT_HAS_LINK TRUE)
    message(STATUS "Ableton Link SDK found — Link sync enabled")
else()
    set(DYSEKT_HAS_LINK FALSE)
    message(STATUS "Ableton Link SDK NOT found — Link sync disabled.")
    message(STATUS "  To enable: git submodule add https://github.com/Ableton/link.git link")
endif()

# --- sfizz (SFZ/SF2 engine — SoundFontLoader slice-render + SfzPlayer live SFZ playback) ---
if(TRUE)
    set(SFIZZ_CLIENTS    OFF CACHE BOOL "" FORCE)
    set(SFIZZ_SHARED     OFF CACHE BOOL "" FORCE)
    set(SFIZZ_JACK       OFF CACHE BOOL "" FORCE)
    set(SFIZZ_LV2        OFF CACHE BOOL "" FORCE)
    set(SFIZZ_VST        OFF CACHE BOOL "" FORCE)
    set(SFIZZ_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(SFIZZ_TESTS      OFF CACHE BOOL "" FORCE)
    # Disable sfizz_render CLI tool — it pulls in cxxopts which collides
    # with vcpkg's pre-registered cxxopts alias target on Windows CI.
    set(SFIZZ_RENDER     OFF CACHE BOOL "" FORCE)
    add_subdirectory(sfizz EXCLUDE_FROM_ALL)
    set(DYSEKT_HAS_SFIZZ TRUE)
    message(STATUS "sfizz found — SF2/SFZ slice-render support enabled")
else()
    set(DYSEKT_HAS_SFIZZ FALSE)
    message(STATUS "sfizz NOT found at ./sfizz — SF2/SFZ slice-render support disabled.")
endif()

# --- FluidSynth (SF2 live-playback engine — used by SfzPlayer) ---
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

# Resolve the sfizz C API include directory.
# sfizz.h (C API) lives at sfizz/src/sfizz.h in the sfizz source tree.
# The sfizz::sfizz CMake target only exposes C++ headers, so we add it manually.
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

# ────────────────────────────────────────────────────────────────────────────
#  DYSEKT-SF VST3 Plugin
# ────────────────────────────────────────────────────────────────────────────
juce_add_plugin(Dysekt
    COMPANY_NAME "DunSoft"
    PLUGIN_MANUFACTURER_CODE Dnst
    PLUGIN_CODE Dysk
    FORMATS VST3
    PRODUCT_NAME "DYSEKT-SF"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# Use a consistent source encoding. Release builds deliberately omit /Zi and
# /DEBUG so public packages do not contain large debugging-symbol files.
if(MSVC)
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
    src/ui/assets/icons/icon_mute.svg
    src/ui/assets/icons/icon_solo.svg
    src/ui/assets/icons/icon_lock.svg
    src/ui/assets/icons/icon_power.svg
    src/ui/assets/icons/icon_pan.svg
    src/ui/assets/icons/icon_volume.svg
    src/ui/assets/buttons/button_idle.svg
    src/ui/assets/buttons/button_hover.svg
    src/ui/assets/buttons/button_active.svg
    src/ui/assets/knobs/knob_face.svg
    src/ui/assets/knobs/knob_pointer.svg
    src/ui/assets/knobs/knob_sprite_64x64_64frames.svg
    src/ui/assets/knobs/knob_sprite_64x64_64frames.png
    src/ui/assets/logos/DYSEKT-SF_logo_cr8.png
    src/ui/assets/logos/DYSEKT-SF_logo_dark.png
    src/ui/assets/logos/DYSEKT-SF_logo_dysekt.png
    src/ui/assets/logos/DYSEKT-SF_logo_ghost.png
    src/ui/assets/logos/DYSEKT-SF_logo_hack.png
    src/ui/assets/logos/DYSEKT-SF_logo_lazy.png
    src/ui/assets/logos/DYSEKT-SF_logo_midnight.png
    src/ui/assets/logos/DYSEKT-SF_logo_opendaw.png
    src/ui/assets/logos/DYSEKT-SF_logo_pigments.png
    src/ui/assets/logos/DYSEKT-SF_logo_serum.png
    src/ui/assets/logos/DYSEKT-SF_logo_shell.png
    src/ui/assets/logos/DYSEKT-SF_logo_snow.png
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
    src/ui/IconManager.cpp
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
    src/ui/Sf2LcdDisplay.cpp
    src/ui/Sf2WaveformLcd.cpp
    src/ui/DualLcdControlFrame.cpp
    src/ui/SfzDropdownPanel.cpp
    src/ui/SfzPlayerDropdownPanel.cpp
    src/ui/Sf2ProgramGrid.cpp
    src/ui/SfzModulePanel.cpp
    src/ui/WaveformOverview.cpp
    src/ui/ArchiveIntegration.cpp
    src/ui/GlobalEqPanel.cpp
    # SequencerEngine and MidiClip are standalone-only — the DAW is the sequencer
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
    DYSEKT_HAS_LINK=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    DYSEKT_HAS_SFIZZ=$<BOOL:${DYSEKT_HAS_SFIZZ}>
    DYSEKT_HAS_FLUIDSYNTH=$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>
    DYSEKT_STANDALONE=0
)

target_link_libraries(Dysekt
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        DysektFonts
        $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:sfizz::sfizz>
        $<$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>:FluidSynth::libfluidsynth>
        $<$<PLATFORM_ID:Linux>:asound>
        chowdsp::chowdsp_filters
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ────────────────────────────────────────────────────────────────────────────
#  DYSEKT-SF Standalone App
#  Same engine + UI as the plugin, plus piano roll, pattern manager,
#  and audio device selection.
# ────────────────────────────────────────────────────────────────────────────
juce_add_gui_app(DysektStandalone
    PRODUCT_NAME "DYSEKT-SF"
    # JUCE's CMake icon generator reads raster source images and generates the
    # Windows .ico file during configuration. Do not pass an .ico as input.
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/DYSEKT-SF.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/DYSEKT-SF.png"
)

# Release packages omit debugging symbols.
if(MSVC)
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
    src/ui/IconManager.cpp
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
    src/ui/Sf2LcdDisplay.cpp
    src/ui/Sf2WaveformLcd.cpp
    src/ui/DualLcdControlFrame.cpp
    src/ui/SfzDropdownPanel.cpp
    src/ui/SfzPlayerDropdownPanel.cpp
    src/ui/Sf2ProgramGrid.cpp
    src/ui/SfzModulePanel.cpp
    src/ui/WaveformOverview.cpp
    src/ui/ArchiveIntegration.cpp
    src/ui/GlobalEqPanel.cpp
    src/sequencer/SequencerEngine.cpp
    src/sequencer/MidiClip.cpp
    src/standalone/Main.cpp
    src/standalone/MainWindow.cpp
)

target_include_directories(DysektStandalone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${DYSEKT_HAS_LINK}>:${CMAKE_CURRENT_SOURCE_DIR}/link/include>
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-stretch
    ${CMAKE_CURRENT_SOURCE_DIR}/signalsmith-linear
    $<$<BOOL:${DYSEKT_HAS_SFIZZ}>:${SFIZZ_C_API_INCLUDE}>
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/common>
)

target_compile_definitions(DysektStandalone PUBLIC
    JUCE_USE_CURL=0
    DYSEKT_HAS_LINK=$<BOOL:${DYSEKT_HAS_LINK}>
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_WEB_BROWSER=0
    DYSEKT_HAS_SFIZZ=$<BOOL:${DYSEKT_HAS_SFIZZ}>
    DYSEKT_HAS_FLUIDSYNTH=$<BOOL:${DYSEKT_HAS_FLUIDSYNTH}>
    DYSEKT_STANDALONE=1
    $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
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
        chowdsp::chowdsp_filters
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
