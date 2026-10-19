cmake_minimum_required(VERSION 3.22)

project(RiddimMachine VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically, so the Windows standalone .exe runs on a fresh PC
# without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use a local JUCE checkout with -DJUCE_DIR=/path/to/JUCE, otherwise fetch it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} juce)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(RIDDIM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RIDDIM_FORMATS AU)
endif()

# Code shared by the synth and the FX plugin.
set(RIDDIM_SHARED_SOURCES
    Source/Parameters.cpp
    Source/PresetManager.cpp
    Source/FactoryPresets.cpp
    Source/Visualiser3D.cpp
    Source/dsp/RiddimEngine.cpp
    Source/dsp/RiddimFx.cpp
    Source/dsp/Wavetables.cpp
    Source/ui/ControlPanel.cpp
    Source/ui/PresetBar.cpp)

function(riddim_configure target)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# The synth.
juce_add_plugin(RiddimMachine
    COMPANY_NAME "Riddim Machine"
    PRODUCT_NAME "Riddim Machine"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Rdmc
    PLUGIN_CODE Rdm1
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${RIDDIM_FORMATS})

target_sources(RiddimMachine PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${RIDDIM_SHARED_SOURCES})
riddim_configure(RiddimMachine)

# The riddim processing chain as an effect for any track.
juce_add_plugin(RiddimMachineFX
    COMPANY_NAME "Riddim Machine"
    PRODUCT_NAME "Riddim Machine FX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Rdmc
    PLUGIN_CODE Rdm2
    VST3_CATEGORIES Fx Distortion Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${RIDDIM_FORMATS})

target_sources(RiddimMachineFX PRIVATE
    Source/fx/FxProcessor.cpp
    Source/fx/FxEditor.cpp
    ${RIDDIM_SHARED_SOURCES})
riddim_configure(RiddimMachineFX)

# Riddim Forge: a second synth with dual warps, wavetable import, a hit sequencer and a mod matrix.
juce_add_plugin(RiddimForge
    COMPANY_NAME "Riddim Machine"
    PRODUCT_NAME "Riddim Forge"
    BUNDLE_ID com.riddimmachine.riddimforge
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_MANUFACTURER_CODE Rdmc
    PLUGIN_CODE Rdm3
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${RIDDIM_FORMATS})

# Factory presets, compiled in.
juce_add_binary_data(ForgeBinaryData
    HEADER_NAME ForgeBinaryData.h
    NAMESPACE ForgeBinaryData
    SOURCES Presets/forge_factory.json)

set(FORGE_DSP_SOURCES
    Source/forge/ForgeEngine.cpp
    Source/forge/ForgeFilter.cpp
    Source/forge/ForgeRack.cpp
    Source/forge/ForgeRenderer.cpp
    Source/forge/ForgeParameters.cpp
    Source/forge/ForgePresets.cpp
    Source/forge/ForgeMagic.cpp)

target_sources(RiddimForge PRIVATE
    ${FORGE_DSP_SOURCES}
    Source/forge/ForgeUI.cpp
    Source/forge/ForgeVisual.cpp
    Source/forge/ForgeProcessor.cpp
    Source/forge/ForgeEditor.cpp
    ${RIDDIM_SHARED_SOURCES})
riddim_configure(RiddimForge)
target_link_libraries(RiddimForge PRIVATE ForgeBinaryData)

# Offline render check: plays riddim patterns through the engine and FX chain and writes WAVs.
option(RIDDIM_BUILD_TESTS "Build the offline render test" OFF)

if(RIDDIM_BUILD_TESTS)
    juce_add_console_app(RiddimRenderTest PRODUCT_NAME "RiddimRenderTest")
    target_sources(RiddimRenderTest PRIVATE
        Tests/RenderTest.cpp
        Source/dsp/RiddimEngine.cpp
        Source/dsp/RiddimFx.cpp
        Source/dsp/Wavetables.cpp)
    target_compile_definitions(RiddimRenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RiddimRenderTest PRIVATE
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    juce_add_console_app(ForgeRenderTest PRODUCT_NAME "ForgeRenderTest")
    target_sources(ForgeRenderTest PRIVATE
        Tests/ForgeRenderTest.cpp
        ${FORGE_DSP_SOURCES}
        Source/Parameters.cpp
        Source/PresetManager.cpp
        Source/FactoryPresets.cpp
        Source/dsp/RiddimEngine.cpp
        Source/dsp/RiddimFx.cpp
        Source/dsp/Wavetables.cpp)
    target_compile_definitions(ForgeRenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ForgeRenderTest PRIVATE
        ForgeBinaryData
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Sound matching: searches for synth settings that sound like a reference clip.
    juce_add_console_app(RiddimMatch PRODUCT_NAME "RiddimMatch")
    target_sources(RiddimMatch PRIVATE
        Tools/RiddimMatch.cpp
        Source/dsp/RiddimEngine.cpp
        Source/dsp/RiddimFx.cpp
        Source/dsp/Wavetables.cpp)
    target_compile_definitions(RiddimMatch PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(RiddimMatch PRIVATE
        juce::juce_audio_formats juce::juce_audio_processors juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
