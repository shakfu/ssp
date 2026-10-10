cmake_minimum_required(VERSION 3.15)
project(SCSYNTH VERSION 0.1.0)

# built by scripts/build_deps.sh ssp|host; see docs/BUILDING.md
if (TARGET_SSP)
    set(SCSYNTH_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
else ()
    set(SCSYNTH_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
endif ()
set(SC "${SCSYNTH_DEPS}/include/supercollider")

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(SCSYNTH
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "SuperCollider scsynth host"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE SCSY
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "scsy")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(SCSYNTH
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/ScsynthEngine.cpp
        Source/ScWorld.cpp
        Source/ScsyDef.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )

# scsynth's headers and defines only where they are used: they must match scripts/scsynth/CMakeLists.txt
set_source_files_properties(Source/ScWorld.cpp PROPERTIES
        COMPILE_DEFINITIONS "SC_AUDIO_API=SC_AUDIO_API_SSP;SC_AUDIO_API_SSP=100;SC_MEMORY_ALIGNMENT=32"
        INCLUDE_DIRECTORIES "${SC}/include/common;${SC}/common;${SC}/include/server;${SC}/include/plugin_interface;${SC}/server/scsynth;${SC}/external_libraries/boost;${SC}/external_libraries/boost_sync/include;${SC}/external_libraries/TLSF-2.4.6/src;${SCSYNTH_DEPS}/include")

target_compile_definitions(SCSYNTH
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

# gcc 8 keeps std::filesystem in libstdc++fs, beside the toolchain's libstdc++ rather than in the
# sysroot; glibc 2.32 keeps shm_open in librt
if (TARGET_SSP)
    set(STDCXXFS "${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a")
else ()
    set(STDCXXFS stdc++fs)
endif ()
target_link_libraries(SCSYNTH PRIVATE
        juce::juce_audio_utils
        ${SCSYNTH_DEPS}/lib/libscsynth.a
        ${SCSYNTH_DEPS}/lib/libsndfile.a
        ${STDCXXFS} rt dl)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
