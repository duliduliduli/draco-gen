# Draco Gen - AI-assisted wavetable synth for hybrid trap / tearout / dark bass.
# Based on the Vital synthesizer source (GPL-3.0). This project is GPL-3.0.

cmake_minimum_required(VERSION 3.15)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(DracoGen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DRACO_BUILD_PLUGIN "Build the VST3/AU/Standalone plugin" ON)
option(DRACO_BUILD_RENDER "Build the headless draco_render command line tool" ON)
option(DRACO_BUILD_TESTS "Build the generator unit tests" ON)

enable_testing()

add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)

set(DRACO_COMPANY_NAME "Draco Gen")
set(DRACO_PRODUCT_NAME "Draco Gen")
set(DRACO_BUNDLE_ID "com.dracogen.synth")

# ------------------------------------------------------------------------------
# Embedded resources
# ------------------------------------------------------------------------------
file(GLOB DRACO_FONTS CONFIGURE_DEPENDS resources/fonts/*.ttf resources/fonts/*.otf)
file(GLOB DRACO_ICONS CONFIGURE_DEPENDS resources/icons/*.svg)
file(GLOB DRACO_TUNINGS CONFIGURE_DEPENDS resources/tunings/*.scl)
file(GLOB DRACO_GENERATOR_CONFIGS CONFIGURE_DEPENDS resources/generator/*.json)

juce_add_binary_data(DracoBinaryData
  HEADER_NAME BinaryData.h
  NAMESPACE BinaryData
  SOURCES
    ${DRACO_FONTS}
    ${DRACO_ICONS}
    ${DRACO_TUNINGS}
    ${DRACO_GENERATOR_CONFIGS}
    resources/default.vitalskin)

set_target_properties(DracoBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# Shared settings
# ------------------------------------------------------------------------------
set(DRACO_INCLUDE_DIRS
  src/common
  src/common/wavetable
  src/generator
  src/interface/editor_components
  src/interface/editor_sections
  src/interface/look_and_feel
  src/interface/wavetable
  src/interface/wavetable/editors
  src/interface/wavetable/overlays
  src/plugin
  src/synthesis/synth_engine
  src/synthesis/effects
  src/synthesis/filters
  src/synthesis/framework
  src/synthesis/lookups
  src/synthesis/modulators
  src/synthesis/modules
  src/synthesis/producers
  src/synthesis/utilities
  third_party)

string(TIMESTAMP DRACO_BUILD_DATE "%Y %m %d %H %M")

function(draco_configure_target target)
  target_include_directories(${target} PRIVATE ${DRACO_INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE
    NO_AUTH=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_XRANDR=0
    JUCE_STRICT_REFCOUNTEDPOINTER=0
    "BUILD_DATE=${DRACO_BUILD_DATE}"
    DRACO_VERSION_STRING="${PROJECT_VERSION}")

  if(MSVC)
    target_compile_definitions(${target} PRIVATE
      _ENABLE_EXTENDED_ALIGNED_STORAGE
      _USE_MATH_DEFINES
      NOMINMAX
      __SSE2__=1)
    target_compile_options(${target} PRIVATE /bigobj /fp:fast /MP /wd4244 /wd4267 /wd4305)
  else()
    target_compile_options(${target} PRIVATE -ffast-math -fvisibility=hidden -fvisibility-inlines-hidden
      -Wno-deprecated-declarations)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT APPLE)
      target_compile_options(${target} PRIVATE -msse2)
    endif()
  endif()
endfunction()

# ------------------------------------------------------------------------------
# Plugin: VST3 / AU / Standalone
# ------------------------------------------------------------------------------
if(DRACO_BUILD_PLUGIN)
  set(DRACO_FORMATS VST3 Standalone)
  if(APPLE)
    list(APPEND DRACO_FORMATS AU)
  endif()

  juce_add_plugin(DracoGen
    PRODUCT_NAME "${DRACO_PRODUCT_NAME}"
    COMPANY_NAME "${DRACO_COMPANY_NAME}"
    COMPANY_WEBSITE "https://github.com/duliduliduli/draco-gen"
    COMPANY_COPYRIGHT "GPL-3.0. Based on Vital by Matt Tytel."
    BUNDLE_ID "${DRACO_BUNDLE_ID}"
    VERSION "${PROJECT_VERSION}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/draco_app_icon.png"
    FORMATS ${DRACO_FORMATS}
    PLUGIN_MANUFACTURER_CODE Drco
    PLUGIN_CODE DrGn
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_EXPORT_PREFIX DracoGenAU
    VST3_CATEGORIES Instrument Synth
    HARDENED_RUNTIME_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(DracoGen PRIVATE
    src/unity_build/common.cpp
    src/unity_build/interface_editor_components.cpp
    src/unity_build/interface_editor_sections.cpp
    src/unity_build/interface_editor_sections2.cpp
    src/unity_build/interface_look_and_feel.cpp
    src/unity_build/interface_wavetable.cpp
    src/unity_build/plugin.cpp
    src/unity_build/synthesis.cpp
    src/unity_build/generator.cpp)

  juce_generate_juce_header(DracoGen)
  draco_configure_target(DracoGen)
  target_compile_definitions(DracoGen PUBLIC
    JUCE_OPENGL3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_JACK_CLIENT_NAME="DracoGen"
    JUCE_ALSA_MIDI_INPUT_NAME="DracoGen"
    JUCE_ALSA_MIDI_OUTPUT_NAME="DracoGen")

  target_link_libraries(DracoGen
    PRIVATE
      DracoBinaryData
      juce::juce_audio_basics
      juce::juce_audio_devices
      juce::juce_audio_formats
      juce::juce_audio_plugin_client
      juce::juce_audio_processors
      juce::juce_audio_utils
      juce::juce_core
      juce::juce_data_structures
      juce::juce_dsp
      juce::juce_events
      juce::juce_graphics
      juce::juce_gui_basics
      juce::juce_gui_extra
      juce::juce_opengl
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags)
endif()

# ------------------------------------------------------------------------------
# Headless renderer / generator CLI (used by CI smoke tests and data tools)
# ------------------------------------------------------------------------------
if(DRACO_BUILD_RENDER)
  juce_add_console_app(draco_render
    PRODUCT_NAME "draco_render"
    COMPANY_NAME "${DRACO_COMPANY_NAME}")

  target_sources(draco_render PRIVATE
    src/unity_build/common.cpp
    src/unity_build/synthesis.cpp
    src/unity_build/generator.cpp
    src/headless/main.cpp)

  juce_generate_juce_header(draco_render)
  draco_configure_target(draco_render)
  target_compile_definitions(draco_render PRIVATE HEADLESS=1)

  target_link_libraries(draco_render
    PRIVATE
      DracoBinaryData
      juce::juce_audio_basics
      juce::juce_audio_formats
      juce::juce_core
      juce::juce_data_structures
      juce::juce_dsp
      juce::juce_events
    PUBLIC
      juce::juce_recommended_config_flags)
endif()
