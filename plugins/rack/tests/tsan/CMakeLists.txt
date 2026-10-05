# Host build of Track under ThreadSanitizer; configured and run by ../test_track_tsan.py.
cmake_minimum_required(VERSION 3.15)
project(rack_tsan CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif ()

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(RACK "${ROOT}/plugins/rack/Source")
set(COMMON "${ROOT}/plugins/common")
set(JUCE_MODULES "${ROOT}/juce/modules")

set(JUCE_DEFS
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JucePlugin_Name="rack"
        JUCE_MODULE_AVAILABLE_juce_core=1
        JUCE_MODULE_AVAILABLE_juce_audio_basics=1)

# Track uses only juce_core and juce_audio_basics; the other module headers are parsed, not built
add_library(juce_min STATIC
        ${JUCE_MODULES}/juce_core/juce_core.cpp
        ${JUCE_MODULES}/juce_core/juce_core_CompilationTime.cpp
        ${JUCE_MODULES}/juce_audio_basics/juce_audio_basics.cpp
        colour_stub.cpp)
set_target_properties(juce_min PROPERTIES POSITION_INDEPENDENT_CODE ON)  # also linked into the plugins
target_include_directories(juce_min PUBLIC ${JUCE_MODULES})
target_compile_definitions(juce_min PUBLIC ${JUCE_DEFS})
target_compile_options(juce_min PUBLIC -fsanitize=thread -g -O1 -w)
target_link_options(juce_min PUBLIC -fsanitize=thread)
target_link_libraries(juce_min PUBLIC pthread dl)

add_executable(track_race_test
        track_race_test.cpp
        ${RACK}/Track.cpp
        ${RACK}/Module.cpp
        ${RACK}/Matrix.cpp
        ${RACK}/JsonPreset.cpp
        ${COMMON}/ssp/Log.cpp)
target_include_directories(track_race_test PRIVATE ${RACK} ${COMMON})
target_link_libraries(track_race_test PRIVATE juce_min)
# sanitizers abort on RTLD_DEEPBIND, which Module::loadModule passes; the test strips it
target_link_options(track_race_test PRIVATE -Wl,--wrap=dlopen)

# two plugins with different channel counts, so swapping them resizes the module buffer
foreach (CH 2 6)
    add_library(pass${CH} MODULE test_plugin.cpp)
    target_include_directories(pass${CH} PRIVATE ${COMMON})
    target_compile_definitions(pass${CH} PRIVATE CHANNELS=${CH})
    target_link_libraries(pass${CH} PRIVATE juce_min)
    set_target_properties(pass${CH} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
endforeach ()
