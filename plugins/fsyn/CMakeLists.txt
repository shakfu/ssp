cmake_minimum_required(VERSION 3.15)
project(FSYN VERSION 0.1.0)  # see CHANGELOG.md

set(COMMON "${PROJECT_SOURCE_DIR}/../common")
include_directories("${COMMON}")

juce_add_plugin(FSYN
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "SoundFont synth (FluidSynth)"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE FSYN
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "fsyn")

include(${COMMON}/CMakeLists.txt)
include(${COMMON}/engine/CMakeLists.txt)

target_sources(FSYN
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/FluidEngine.cpp
        ${COMMON_SRC}
        ${ENGINE_SRC}
        )

target_compile_definitions(FSYN
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

# FluidSynth, from scripts/build_fluidsynth.sh (DEPS_DIR); its symbols stay inside fsyn.so
target_include_directories(FSYN PRIVATE ${DEPS_DIR}/include)
target_link_libraries(FSYN PRIVATE
        juce::juce_audio_utils
        ${DEPS_DIR}/lib/libfluidsynth.a
        m)
target_link_options(FSYN_VST3 PRIVATE -Wl,--exclude-libs,libfluidsynth.a)
if (TARGET_SSP)  # GCC 8's std::filesystem, which FluidSynth uses
    target_link_libraries(FSYN PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a)
endif ()

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
