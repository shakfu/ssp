cmake_minimum_required(VERSION 3.15)
project(TREMOLO VERSION 0.1.0)

# a plugin in plugins/<name> uses ../common; this one lives in examples/
set(COMMON "${PROJECT_SOURCE_DIR}/../../plugins/common")
include_directories("${COMMON}")

juce_add_plugin(TREMOLO
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "stereo tremolo, compiled from Faust"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE TREM
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "trem")

include(${COMMON}/CMakeLists.txt)
include(${COMMON}/engine/CMakeLists.txt)

target_sources(TREMOLO
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${FAUST_SRC}
        )

target_compile_definitions(TREMOLO
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(TREMOLO PRIVATE
        juce::juce_audio_utils)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
