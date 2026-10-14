cmake_minimum_required(VERSION 3.22)

project(OctoSlice VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime: the plugin works without installing the VC++ redistributable
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(OCTOSLICE_BUILD_TESTS "Build the offline test runner" OFF)
set(JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if (JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

juce_add_plugin(OctoSlice
    COMPANY_NAME            "OctoBio"
    COMPANY_WEBSITE         "https://github.com/OctoBio"
    BUNDLE_ID               com.octobio.octoslice
    PLUGIN_MANUFACTURER_CODE Octb
    PLUGIN_CODE             OSlc
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "OctoSlice"
    DESCRIPTION             "Sample slicer instrument"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES         Instrument Sampler)

set(OCTOSLICE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SliceDetector.cpp
    Source/WaveformView.cpp
    Source/SpaceBackground.cpp
    Source/OctoLookAndFeel.cpp)

target_sources(OctoSlice PRIVATE ${OCTOSLICE_SOURCES})

set(OCTOSLICE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1)

target_compile_definitions(OctoSlice PUBLIC ${OCTOSLICE_DEFINES})

target_link_libraries(OctoSlice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if (OCTOSLICE_BUILD_TESTS)
    juce_add_console_app(OctoSliceTests PRODUCT_NAME "OctoSliceTests")
    target_sources(OctoSliceTests PRIVATE ${OCTOSLICE_SOURCES} Tests/TestMain.cpp)
    target_compile_definitions(OctoSliceTests PRIVATE ${OCTOSLICE_DEFINES}
        JucePlugin_Build_Standalone=0 JucePlugin_Name="OctoSlice" JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_include_directories(OctoSliceTests PRIVATE Source)
    target_link_libraries(OctoSliceTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
