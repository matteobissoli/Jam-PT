cmake_minimum_required(VERSION 3.22)

project(JamPT VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS YES)

if (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(
        -Wno-implicit-int-conversion
        -Wno-implicit-int-conversion-on-negation
        -Wno-switch
        -Wno-switch-enum
    )
endif()

option(JAMPT_FETCH_JUCE "Fetch JUCE automatically with FetchContent" ON)
set(JAMPT_JUCE_GIT_TAG "8.0.8" CACHE STRING "JUCE git tag used when JAMPT_FETCH_JUCE is ON")

if (JAMPT_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JAMPT_JUCE_GIT_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(juce)
else()
    if (NOT DEFINED JUCE_SOURCE_DIR)
        message(FATAL_ERROR "Set JUCE_SOURCE_DIR to a local JUCE checkout, or enable JAMPT_FETCH_JUCE.")
    endif()
    add_subdirectory(${JUCE_SOURCE_DIR} JUCE)
endif()

juce_add_plugin(Jam-PT
    COMPANY_NAME "Matteo Bissoli"
    BUNDLE_ID "com.matteobissoli.jampt"
    PLUGIN_MANUFACTURER_CODE MtBs
    PLUGIN_CODE JmPt
    FORMATS AU Standalone
    PRODUCT_NAME "Jam-PT"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
)

juce_generate_juce_header(Jam-PT)

target_sources(Jam-PT
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/AudioFilePlayer.cpp
        src/CoreMLProcessor.mm
)

target_include_directories(Jam-PT
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(Jam-PT
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

find_library(COREML_FRAMEWORK CoreML REQUIRED)
find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

target_link_libraries(Jam-PT
    PRIVATE
        ${COREML_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (APPLE)
    set_target_properties(Jam-PT PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC NO
        XCODE_ATTRIBUTE_OTHER_LIBTOOLFLAGS "-no_warning_for_no_symbols"
    )
endif()
