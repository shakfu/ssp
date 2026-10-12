cmake_minimum_required(VERSION 3.15)
project(sspdev)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# every plugin compiles its own copy of the JUCE modules; ccache makes clean rebuilds cheap
find_program(CCACHE_PROGRAM ccache)
if (CCACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif ()
#set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message("apple config")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (CMAKE_CROSSCOMPILING)
        message("cross compile config")
    else ()
        message("linux native build - NO crosscompile")
    endif ()
endif ()


# JUCE builds from a patched copy, so the submodule stays at its commit. The copy is redone when the
# submodule's commit or a patch changes.
file(GLOB JUCE_PATCHES ${CMAKE_SOURCE_DIR}/scripts/patches/juce-*.patch)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${JUCE_PATCHES})
execute_process(COMMAND git -C ${CMAKE_SOURCE_DIR}/juce rev-parse HEAD
        OUTPUT_VARIABLE JUCE_STAMP OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
foreach (p ${JUCE_PATCHES})
    file(SHA256 ${p} h)
    string(APPEND JUCE_STAMP " ${h}")
endforeach ()
set(JUCE_PATCHED ${CMAKE_BINARY_DIR}/juce-patched)
set(JUCE_OLD_STAMP "")
if (EXISTS ${JUCE_PATCHED}/.ssp-stamp)
    file(READ ${JUCE_PATCHED}/.ssp-stamp JUCE_OLD_STAMP)
endif ()
if (NOT JUCE_OLD_STAMP STREQUAL JUCE_STAMP)
    file(REMOVE_RECURSE ${JUCE_PATCHED})
    # juceaide's own build caches the JUCE source path; it rebuilds from the new copy
    file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/juce/tools)
    file(COPY ${CMAKE_SOURCE_DIR}/juce/ DESTINATION ${JUCE_PATCHED} PATTERN .git EXCLUDE)
    foreach (p ${JUCE_PATCHES})
        execute_process(COMMAND patch -d ${JUCE_PATCHED} -p1 -s -i ${p} RESULT_VARIABLE rc)
        if (NOT rc EQUAL 0)
            message(FATAL_ERROR "${p} does not apply to juce")
        endif ()
    endforeach ()
    file(WRITE ${JUCE_PATCHED}/.ssp-stamp "${JUCE_STAMP}")
endif ()
add_subdirectory(${JUCE_PATCHED} ${CMAKE_BINARY_DIR}/juce)

string(TIMESTAMP TB_BUILD_DATE "%y%m%d")
add_definitions(-DTB_BUILD_DATE="${TB_BUILD_DATE}")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DJUCE_DISABLE_JUCE_VERSION_PRINTING=1")

if (DEFINED ENV{ALWAYS_COMPACT_UI} OR ALWAYS_COMPACT_UI)
    set(ALWAYS_COMPACT_UI 1)
    message("target : SSP, forced compact ui, scale 1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTARGET_SSP=1 -DCOMPACT_UI_SCALE=1 -DFORCE_COMPACT_UI=1")
else()
    message("target : SSP, ssp ui and scale")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DTARGET_SSP=1 -DCOMPACT_UI_SCALE=2")
endif()

if(APPLE AND NOT ALWAYS_COMPACT_UI) 
    message("install plugins enabled")
    set(INSTALL_PLUGIN 1)
else() 
    message("install plugins disabled")
set(INSTALL_PLUGIN 0)
endif()


add_subdirectory(plugins)

# the plugin guides' examples: built and tested on the host, never for the SSP or a release
if (NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(examples/svca)
    add_subdirectory(examples/tremolo)
endif ()

# set(CMAKE_FOLDER .)
function(_juce_add_pips)
    file(GLOB headers
            CONFIGURE_DEPENDS LIST_DIRECTORIES false
            "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    endif ()

    foreach (header IN ITEMS ${headers})
        juce_add_pip(${header} added_target)
        target_link_libraries(${added_target} PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
    endforeach ()
endfunction()

