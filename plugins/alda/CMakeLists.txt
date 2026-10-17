cmake_minimum_required(VERSION 3.15)
project(ALDA VERSION 0.1.0)  # see CHANGELOG.md

# psnd's Alda core, from scripts/fetch_alda.sh (ALDA_DIR): the parser, interpreter and event list, without
# psnd's playback and MIDI backends. Its own library, so its include path, whose headers have names such as
# error.h and context.h, stays out of the plugin's.
file(GLOB ALDA_CORE_SRC "${ALDA_DIR}/impl/*.c")
add_library(alda_core STATIC ${ALDA_CORE_SRC} ${ALDA_DIR}/scheduler.c Source/AldaCore.c)
target_include_directories(alda_core
        PUBLIC ${ALDA_DIR}/include Source/shim
        PRIVATE ${ALDA_DIR}/include/alda ${ALDA_DIR}/impl)
target_compile_options(alda_core PRIVATE -std=gnu11)
set_target_properties(alda_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(ALDA
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "Alda sequencer"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE ALDA
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "alda")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(ALDA
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/AldaEngine.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )
ssp_highlights(ALDA alda)  # the code view's colours

target_compile_definitions(ALDA
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1
        JUCE_ALSA_MIDI_NAME="alda")  # the ALSA client holding the module's MIDI port; JUCE's default is "JUCE"

target_link_libraries(ALDA PRIVATE
        juce::juce_audio_utils
        alda_core
        m)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
