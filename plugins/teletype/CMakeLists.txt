cmake_minimum_required(VERSION 3.15)
project(TELETYPE VERSION 0.1.0 LANGUAGES C CXX)  # see CHANGELOG.md
# xcSSP.cmake gives C and C++ the cross target and CPU flags; the assembler needs the same
set(CMAKE_ASM_COMPILER_TARGET "${CMAKE_C_COMPILER_TARGET}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS}")
enable_language(ASM)

# the firmware is its own library, embedded as data; one copy is loaded per instance
add_subdirectory(firmware)
set(TTYP_FIRMWARE "${CMAKE_CURRENT_BINARY_DIR}/firmware/ttyp_firmware.so")
configure_file(firmware/firmware_image.S.in "${CMAKE_CURRENT_BINARY_DIR}/firmware_image.S" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/firmware_image.S" PROPERTIES OBJECT_DEPENDS "${TTYP_FIRMWARE}")

include_directories("${PROJECT_SOURCE_DIR}/../common")

juce_add_plugin(TELETYPE
        COMPANY_NAME "shakfu"
        COMPANY_WEBSITE "github.com/shakfu"
        COMPANY_EMAIL "shakfu@users.noreply.github.com"
        DESCRIPTION "monome teletype"
        COPY_PLUGIN_AFTER_BUILD FALSE
        PLUGIN_MANUFACTURER_CODE SF00
        PLUGIN_CODE TTYP
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE
        PRODUCT_NAME "ttyp")

include(${CMAKE_CURRENT_SOURCE_DIR}/../common/CMakeLists.txt)

target_sources(TELETYPE
        PUBLIC
        Source/SSPApi.cpp
        PRIVATE
        Source/Firmware.cpp
        Source/KeyInput.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Teletype.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/firmware_image.S"
        ${COMMON_SRC}
        )
add_dependencies(TELETYPE ttyp_firmware)

target_compile_definitions(TELETYPE
        PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        SSP_TITLE_NO_MANUFACTURER=1
        SSP_VERSION_FROM_PROJECT=1)

target_link_libraries(TELETYPE PRIVATE
        juce::juce_audio_utils
        ${CMAKE_DL_LIBS})

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES PREFIX "")

if(INSTALL_PLUGIN)
    juce_enable_copy_plugin_step(${PROJECT_NAME})
endif()
