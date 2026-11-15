cmake_minimum_required(VERSION 3.22)
project(PitchFollowExciter VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

# ---- DSP core checks: no JUCE needed -------------------------------------------------
add_executable(pfx_core_test tests/core_test.cpp)
target_include_directories(pfx_core_test PRIVATE core)
enable_testing()
add_test(NAME core COMMAND pfx_core_test)

# ---- Plugin ---------------------------------------------------------------------------
option(PFX_BUILD_PLUGIN "Build the JUCE plugin" ON)
set(PFX_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional; downloaded if empty)")

if(PFX_BUILD_PLUGIN)
    if(PFX_JUCE_PATH)
        add_subdirectory(${PFX_JUCE_PATH} juce EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(juce)
    endif()

    if(APPLE)
        set(PFX_FORMATS VST3 AU Standalone)
    else()
        set(PFX_FORMATS VST3 Standalone)
    endif()

    juce_add_plugin(PitchFollowExciter
        PRODUCT_NAME "Pitch-follow Exciter"
        COMPANY_NAME "PFX Audio"                # placeholder: change before release
        PLUGIN_MANUFACTURER_CODE Pfxa           # placeholder: must be unique to you
        PLUGIN_CODE Pfex
        FORMATS ${PFX_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(PitchFollowExciter PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)
    target_include_directories(PitchFollowExciter PRIVATE core src)
    target_compile_definitions(PitchFollowExciter PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(PitchFollowExciter
        PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
