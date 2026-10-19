cmake_minimum_required(VERSION 3.15)
project(EZEPTOCORE VERSION 0.1.0)  # see CHANGELOG.md

set(COMMON "${PROJECT_SOURCE_DIR}/../common")
include_directories("${COMMON}")

# The firmware, as _core's desktop engine, which scripts/fetch_core.sh generates per target into
# CORE_DIR/gen. Its unit holds clang's preprocessed libc, so clang compiles it on both targets. On the
# SSP it is Thumb-2: clang 18 and 20 miscompile it in ARM mode (docs/dev/ezeptocore.md).
find_program(EZEP_CLANG clang REQUIRED NO_CMAKE_FIND_ROOT_PATH)  # the host's, not the sysroot's
if (TARGET_SSP)
    set(CORE_GEN "${CORE_DIR}/gen/ssp")
    set(CORE_CFLAGS --target=arm-linux-gnueabihf --sysroot=${CMAKE_SYSROOT} -mcpu=cortex-a17 -mfloat-abi=hard
            -mfpu=neon-vfpv4 -mthumb -O3)
else ()
    set(CORE_GEN "${CORE_DIR}/gen/host")
    set(CORE_CFLAGS -O2)
endif ()
set(CORE_OBJ "${CMAKE_CURRENT_BINARY_DIR}/core_engine.o")
add_custom_command(OUTPUT ${CORE_OBJ}
        COMMAND ${EZEP_CLANG} ${CORE_CFLAGS} -fPIC -fvisibility=hidden -fwrapv -ffp-contract=off -std=c11 -w
                -c ${CORE_GEN}/engine.c -o ${CORE_OBJ}
        DEPENDS ${CORE_GEN}/engine.c
        COMMENT "clang: _core's generated engine")
set_source_files_properties(${CORE_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

juce_add_plugin(EZEPTOCORE
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "EZEPTOCORE sample mangler"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE EZEP
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "ezep")

include(${COMMON}/CMakeLists.txt)
include(${COMMON}/engine/CMakeLists.txt)

target_sources(EZEPTOCORE
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/EzepEngine.cpp
        ${CORE_DIR}/rack/src/Storage.cpp
        ${CORE_DIR}/lib/core_engine/card_format.c
        ${CORE_DIR}/lib/pcg_basic.c
        ${CORE_OBJ}
        ${COMMON_SRC}
        ${ENGINE_SRC}
        )
target_include_directories(EZEPTOCORE PRIVATE ${CORE_DIR}/lib/core_engine ${CORE_DIR}/rack/src)

target_compile_definitions(EZEPTOCORE
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(EZEPTOCORE PRIVATE
        juce::juce_audio_utils
        m)
if (TARGET_SSP)  # GCC 8's std::filesystem, which Storage.cpp uses
    target_link_libraries(EZEPTOCORE PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a)
endif ()

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
