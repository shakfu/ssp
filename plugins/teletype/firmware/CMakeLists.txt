# The teletype firmware as a shared library: monome teletype and libavr32 against monome-rack's mock
# hardware, as monome-rack's firmware/teletype.mk builds it. scripts/fetch_teletype.sh fetches the
# sources. Standalone, so the native tests build it without JUCE.
cmake_minimum_required(VERSION 3.15)
project(ttyp_firmware C)

set(TT_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../build/deps/src/teletype" CACHE PATH "scripts/fetch_teletype.sh output")
set(TT "${TT_SRC_DIR}/teletype")
set(AVR "${TT_SRC_DIR}/libavr32")
set(MOCK "${TT_SRC_DIR}/monome-rack/firmware/mock_hardware")

file(GLOB FW_SRC
        "${TT}/src/*.c" "${TT}/src/ops/*.c" "${TT}/module/*.c"
        "${AVR}/src/euclidean/*.c"
        "${MOCK}/*.c" "${MOCK}/common/*.c" "${MOCK}/modules/teletype/*.c")
list(FILTER FW_SRC EXCLUDE REGEX "/module/usb_disk_mode\\.c$")
list(APPEND FW_SRC
        "${AVR}/src/events.c" "${AVR}/src/timers.c" "${AVR}/src/util.c" "${AVR}/src/font.c"
        "${AVR}/src/kbd.c" "${AVR}/src/region.c" "${AVR}/src/random.c" "${AVR}/src/usb/hid/hid.c"
        "${AVR}/src/music.c" "${AVR}/src/midi_common.c"
        "${CMAKE_CURRENT_LIST_DIR}/gitversion.c"
        "${CMAKE_CURRENT_LIST_DIR}/ttyp_hid.c")

add_library(ttyp_firmware SHARED ${FW_SRC})
set_target_properties(ttyp_firmware PROPERTIES C_STANDARD 99 PREFIX "" OUTPUT_NAME "ttyp_firmware"
        C_VISIBILITY_PRESET hidden)
# include/ replaces the headers monome-rack's build takes from libavr32/asf and libavr32/conf: they carry
# Atmel's ASF licence, which allows redistribution only with Atmel microcontroller products. Neither
# directory is on the path.
target_include_directories(ttyp_firmware PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${MOCK}" "${MOCK}/include"
        "${AVR}/src" "${AVR}/src/usb/midi" "${AVR}/src/usb/hid" "${AVR}/src/usb/cdc" "${AVR}/src/usb/ftdi"
        "${TT}/src" "${TT}/src/ops" "${TT}"
        "${TT_SRC_DIR}/monome-rack/lib/cbbq")
# ARCH_LIN or ARCH_MAC selects the mock API's export attribute; Rack's arch.mk sets it for monome-rack
target_compile_definitions(ttyp_firmware PRIVATE __AVR32_UC3B0512__ NULL=0 ARCH_AVR32=1
        $<IF:$<PLATFORM_ID:Darwin>,ARCH_MAC=1,ARCH_LIN=1>)
# upstream code: its warnings are not ours to fix, but implicit declarations are bugs on ARM
target_compile_options(ttyp_firmware PRIVATE -w -Werror=implicit-function-declaration)
target_link_libraries(ttyp_firmware PRIVATE m)
