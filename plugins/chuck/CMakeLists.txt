cmake_minimum_required(VERSION 3.15)
project(CHUCK VERSION 0.1.0)

# built by scripts/build_deps.sh ssp|host; see docs/BUILDING.md
if (TARGET_SSP)
    set(CHUCK_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
else ()
    set(CHUCK_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
endif ()

include_directories("${PROJECT_SOURCE_DIR}/../common")
include_directories("${CHUCK_DEPS}/include/chuck")
# must match CHUCK_DEFS in scripts/build_deps.sh: they change ChucK's class layouts
add_compile_definitions(__LINUX_ALSA__ __CK_SNDFILE_NATIVE__)

juce_add_plugin(CHUCK
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "ChucK host"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE CHUK
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "chuk")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(CHUCK
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/ChuckEngine.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )

target_compile_definitions(CHUCK
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(CHUCK PRIVATE
        juce::juce_audio_utils
        ${CHUCK_DEPS}/lib/libchuck.a
        ${CHUCK_DEPS}/lib/libsndfile.a
        asound)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
