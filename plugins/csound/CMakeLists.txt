cmake_minimum_required(VERSION 3.15)
project(CSOUND VERSION 0.1.0)

# built by scripts/build_deps.sh ssp|host; see docs/BUILDING.md
if (TARGET_SSP)
    set(CSOUND_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
else ()
    set(CSOUND_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
endif ()

include_directories("${PROJECT_SOURCE_DIR}/../common")
include_directories("${CSOUND_DEPS}/include/csound")

juce_add_plugin(CSOUND
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "Csound 7 host"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE CSND
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "csnd")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(CSOUND
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/CsoundEngine.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )

target_compile_definitions(CSOUND
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(CSOUND PRIVATE
        juce::juce_audio_utils
        ${CSOUND_DEPS}/lib/libcsound.a
        ${CSOUND_DEPS}/lib/libsndfile.a)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
