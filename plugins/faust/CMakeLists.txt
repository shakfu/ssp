cmake_minimum_required(VERSION 3.15)
project(FAUST VERSION 0.2.0)

# built by scripts/build_deps.sh ssp|host; see docs/BUILDING.md
# libfaust links the shared libLLVM: the SSP's (in its rootfs, and the sysroot) or the host's
if (TARGET_SSP)
    set(FAUST_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/ssp")
    set(LLVM_LIB_DIR "${CMAKE_SYSROOT}/usr/lib")
else ()
    set(FAUST_DEPS "${PROJECT_SOURCE_DIR}/../../build/deps/host")
    execute_process(COMMAND llvm-config --libdir OUTPUT_VARIABLE LLVM_LIB_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
endif ()

include_directories("${PROJECT_SOURCE_DIR}/../common")
include_directories("${FAUST_DEPS}/include")

juce_add_plugin(FAUST
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "Faust host, through the libfaust LLVM JIT"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE FSTR
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "fstr")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)
include(${CMAKE_CURRENT_SOURCE_DIR}/../common/engine/CMakeLists.txt)

target_sources(FAUST
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/FaustRuntime.cpp
        Source/PluginProcessor.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        ${SCRIPT_SRC}
        )

target_compile_definitions(FAUST
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(FAUST PRIVATE
        juce::juce_audio_utils
        ${FAUST_DEPS}/lib/libfaust.a
        -L${LLVM_LIB_DIR} -lLLVM)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
