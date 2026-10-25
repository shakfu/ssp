cmake_minimum_required(VERSION 3.15)
project(GSEQ VERSION 0.1.0)  # see CHANGELOG.md

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(GSEQ
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "grid sequencer"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE GSEQ
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "gseq")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/launchpad/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/monome/CMakeLists.txt)

target_sources(GSEQ
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/GridView.cpp
        Source/GseqEngine.cpp
        Source/LaunchpadView.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${CLOCK_SRC}
        ${LAUNCHPAD_SRC}
        ${MONOME_SRC}
        )

target_compile_definitions(GSEQ
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1
        JUCE_ALSA_MIDI_NAME="gseq")  # the ALSA client holding the module's MIDI port; JUCE's default is "JUCE"

target_link_libraries(GSEQ PRIVATE juce::juce_audio_utils)
ssp_link_monome(GSEQ)  # a monome grid, where scripts/build_monome.sh has built libmonome

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
