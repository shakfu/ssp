cmake_minimum_required(VERSION 3.15)
project(MLSP VERSION 0.1.0)  # see CHANGELOG.md

# uSEQ's signal engine, from scripts/fetch_useq.sh (USEQ_DIR), built by the fork's own CMake. In its own
# libraries: its headers include utils/string.h, whose String would meet JUCE's in the plugin's sources.
set(USEQ_TESTS OFF CACHE BOOL "" FORCE)
set(USEQ_DEVTOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${USEQ_DIR} useq EXCLUDE_FROM_ALL)
set_target_properties(useq_utils useq_devtools useq_signal_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(MLSP
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "ModuLisp sequencer"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE MLSP
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "mlsp")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(MLSP
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/UseqEngine.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        ${CLOCK_SRC}
        )

target_compile_definitions(MLSP
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(MLSP PRIVATE
        juce::juce_audio_utils
        useq_signal_engine
        m)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
