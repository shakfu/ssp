cmake_minimum_required(VERSION 3.15)
project(SAPF VERSION 0.2.0)  # see CHANGELOG.md

# The sapf fork's engine and FFTW, from scripts/fetch_sapf.sh (SAPF_DIR); libsndfile from scripts/build_deps.sh,
# whose copy is PIC; see docs/BUILDING.md
if (TARGET_SSP)
    set(SAPF_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
else ()
    set(SAPF_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
endif ()
add_library(SndFile::sndfile STATIC IMPORTED)
set_target_properties(SndFile::sndfile PROPERTIES IMPORTED_LOCATION "${SAPF_DEPS}/lib/libsndfile.a"
        INTERFACE_INCLUDE_DIRECTORIES "${SAPF_DEPS}/include")
set(SAPF_ENGINE_ONLY ON)
add_subdirectory(${SAPF_DIR} sapf_fork)

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
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(SAPF
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/SapfEngine.cpp
        Source/Session.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )
target_include_directories(SAPF PRIVATE Source ${SAPF_DIR}/include)
ssp_highlights(SAPF sapf)  # the code's colours

target_compile_definitions(SAPF
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1
        PRIVATE
        USE_REPLXX=0)  # as the engine-only build exports: it changes the layout of sapf's Thread

# gcc 8 keeps std::filesystem in libstdc++fs, beside the toolchain's libstdc++ rather than in the sysroot
if (TARGET_SSP)
    set(STDCXXFS "${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a")
else ()
    set(STDCXXFS stdc++fs)
endif ()
# The engine by file, not as a target: its public warning flags would reach JUCE's sources.
add_dependencies(SAPF sapf_engine fftw3)
target_link_libraries(SAPF PRIVATE
        juce::juce_audio_utils
        $<TARGET_FILE:sapf_engine>
        $<TARGET_FILE:fftw3>
        SndFile::sndfile
        ${STDCXXFS} pthread m)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
