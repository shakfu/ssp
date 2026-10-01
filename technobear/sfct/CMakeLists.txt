cmake_minimum_required(VERSION 3.15)
project(SFCT VERSION 0.1.0)  # see CHANGELOG.md

set(SOFTCUT_DIR "${PROJECT_SOURCE_DIR}/../../external/softcut-lib")

include_directories("${PROJECT_SOURCE_DIR}/../common")
include_directories("${SOFTCUT_DIR}/include")

juce_add_plugin(SFCT
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "softcut 4 track looper"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE SFCT
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "sfct")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)

target_sources(SFCT
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginEditor.cpp
        Source/PluginMiniEditor.cpp
        Source/PluginProcessor.cpp
        ${SOFTCUT_DIR}/src/FadeCurves.cpp
        ${SOFTCUT_DIR}/src/ReadWriteHead.cpp
        ${SOFTCUT_DIR}/src/SubHead.cpp
        ${SOFTCUT_DIR}/src/Svf.cpp
        ${SOFTCUT_DIR}/src/Voice.cpp
        ${COMMON_SRC}
        )

target_compile_definitions(SFCT
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(SFCT PRIVATE
        juce::juce_audio_utils)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
