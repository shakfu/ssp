cmake_minimum_required(VERSION 3.25)
project(PRDT VERSION 0.1.0)

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(PRDT
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "Pure Data host"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE PRDT
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "prdt")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)
add_subdirectory(pd)  # libpd from external/libpd, with Pd's extra/ objects

target_sources(PRDT
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PdEngine.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )

target_compile_definitions(PRDT
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(PRDT PRIVATE juce::juce_audio_utils pd_extra pd_cyclone)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")
# Only the entry points are exported: Pd's own names, such as error and post, stay inside prdt.so.
target_link_options(${PROJECT_NAME}_VST3 PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
set_property(TARGET ${PROJECT_NAME}_VST3 APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
