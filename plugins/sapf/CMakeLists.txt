cmake_minimum_required(VERSION 3.15)
project(SAPF VERSION 0.1.0)  # see CHANGELOG.md

# scsynth from scripts/build_deps.sh; sapf-sc from scripts/fetch_sapf.sh (SAPF_SC_DIR); see docs/BUILDING.md
if (TARGET_SSP)
    set(SCSYNTH_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
else ()
    set(SCSYNTH_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
endif ()
set(SC "${SCSYNTH_DEPS}/include/supercollider")
set(SCSY "${PROJECT_SOURCE_DIR}/../scsynth/Source")

# sapf-sc's interpreter, without replxx. Linked by file, not as a target: its warning flags stay its own.
# USE_REPLXX must match: it changes the layout of sapf's Thread.
set(SAPF_ROOT ${SAPF_SC_DIR})
add_subdirectory(${SAPF_SC_DIR}/src/engine sapf_engine)
set_target_properties(sapf_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(SAPF
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "sapf live coding"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE SAPF
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "sapf")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/keys/CMakeLists.txt)

target_sources(SAPF
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/FilePicker.cpp
        Source/KeyQueue.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/SapfServer.cpp
        Source/Session.cpp
        Source/TextBuffer.cpp
        ${SCSY}/ScWorld.cpp
        ${SCSY}/ScsyDef.cpp
        ${KEYS_SRC}
        ${COMMON_SRC}
        )
target_include_directories(SAPF PRIVATE Source ${SCSY} ${SAPF_SC_DIR}/include)

# scsynth's headers and defines only where they are used, as in plugins/scsynth/CMakeLists.txt
set_source_files_properties(${SCSY}/ScWorld.cpp PROPERTIES
        COMPILE_DEFINITIONS "SC_AUDIO_API=SC_AUDIO_API_SSP;SC_AUDIO_API_SSP=100;SC_MEMORY_ALIGNMENT=32"
        INCLUDE_DIRECTORIES "${SC}/include/common;${SC}/common;${SC}/include/server;${SC}/include/plugin_interface;${SC}/server/scsynth;${SC}/external_libraries/boost;${SC}/external_libraries/boost_sync/include;${SC}/external_libraries/TLSF-2.4.6/src;${SCSYNTH_DEPS}/include")

target_compile_definitions(SAPF
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1
        PRIVATE
        USE_REPLXX=0)

# gcc 8 keeps std::filesystem in libstdc++fs, beside the toolchain's libstdc++ rather than in the
# sysroot; glibc 2.32 keeps shm_open in librt
if (TARGET_SSP)
    set(STDCXXFS "${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a")
else ()
    set(STDCXXFS stdc++fs)
endif ()
add_dependencies(SAPF sapf_engine)
target_link_libraries(SAPF PRIVATE
        juce::juce_audio_utils
        $<TARGET_FILE:sapf_engine>
        ${SCSYNTH_DEPS}/lib/libscsynth.a
        ${SCSYNTH_DEPS}/lib/libsndfile.a
        ${STDCXXFS} rt dl pthread m)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
