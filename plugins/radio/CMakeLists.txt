cmake_minimum_required(VERSION 3.15)
project(RADIO VERSION 0.1.0)

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(RADIO
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "dual virtual RadioMusic"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE RDIO
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "rdio")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(RADIO
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/RadioEngine.cpp
        Source/Station.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        )

target_compile_definitions(RADIO
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(RADIO PRIVATE
        juce::juce_audio_utils)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
