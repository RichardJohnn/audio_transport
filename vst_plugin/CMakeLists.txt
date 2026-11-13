cmake_minimum_required(VERSION 3.15)

project(AudioTransportVST VERSION 1.3.0)

set(CMAKE_CXX_STANDARD 17)

# Add JUCE as a subdirectory (you'll need to clone JUCE first)
# OR use FetchContent to download it automatically:
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)
FetchContent_MakeAvailable(JUCE)

# Find FFTW3 (prefer Homebrew on macOS for ARM64)
if(APPLE)
    # Use Homebrew's FFTW3 on Apple Silicon
    execute_process(COMMAND brew --prefix fftw OUTPUT_VARIABLE FFTW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(FFTW3_INCLUDE_DIRS "${FFTW_PREFIX}/include")
    set(FFTW3_LIBRARY_DIRS "${FFTW_PREFIX}/lib")
    set(FFTW3_LIBRARIES "${FFTW_PREFIX}/lib/libfftw3.dylib")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFTW3 REQUIRED fftw3)
endif()

# Build audio_transport from source as a static library and link it into the
# plugin, so the installed bundle doesn't depend on a dylib in ../build (and
# the plugin build always picks up library changes without a separate build).
set(AUDIO_TRANSPORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
file(GLOB AUDIO_TRANSPORT_SOURCES CONFIGURE_DEPENDS ${AUDIO_TRANSPORT_DIR}/src/*.cpp)
add_library(audio_transport STATIC ${AUDIO_TRANSPORT_SOURCES})
target_include_directories(audio_transport
    PUBLIC ${AUDIO_TRANSPORT_DIR}/include
    PRIVATE ${FFTW3_INCLUDE_DIRS})
set_target_properties(audio_transport PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(audio_transport PRIVATE -O3)

# Plugin settings
juce_add_plugin(AudioTransportVST
    COMPANY_NAME "YourCompany"
    PLUGIN_MANUFACTURER_CODE Yoco
    PLUGIN_CODE Atpr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Audio Transport"
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    IS_SYNTH FALSE
    WANTS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# Source files
target_sources(AudioTransportVST
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
)

# Compile definitions
target_compile_definitions(AudioTransportVST
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
    PRIVATE
        PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        PLUGIN_VERSION_STRING="${PROJECT_VERSION}"
)

# Link libraries
target_link_libraries(AudioTransportVST
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Link audio_transport and FFTW3
target_link_libraries(AudioTransportVST
    PRIVATE
        audio_transport
        ${FFTW3_LIBRARIES}
)

target_include_directories(AudioTransportVST
    PRIVATE
        ${FFTW3_INCLUDE_DIRS}
)
