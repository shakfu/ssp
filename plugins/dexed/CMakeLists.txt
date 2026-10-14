cmake_minimum_required(VERSION 3.15)
project(DEXED VERSION 0.1.0)

set(COMMON "${PROJECT_SOURCE_DIR}/../common")
include_directories("${COMMON}")

juce_add_plugin(DEXED
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "6-operator FM synth (Dexed)"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE DEXD
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "dexd")

include(${COMMON}/CMakeLists.txt)
include(${COMMON}/engine/CMakeLists.txt)

# Synth_Dexed, patched: see Source/synth_dexed/README.md
file(GLOB SYNTH_DEXED_SRC CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/Source/synth_dexed/*.cpp)

target_sources(DEXED
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DexedEngine.cpp
        Source/Cartridge.cpp
        ${SYNTH_DEXED_SRC}
        ${COMMON_SRC}
        ${ENGINE_SRC}
        )

target_compile_definitions(DEXED
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1
        PRIVATE
        DISABLE_DEXED_COMPRESSOR=1)

target_link_libraries(DEXED PRIVATE
        juce::juce_audio_utils)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
