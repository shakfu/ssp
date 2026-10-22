# alda_test and psnd's shared-suite runner, built for the host or, with xcSSP.cmake, for the SSP, against the
# plugin's build of psnd's Alda core. ALDA_DIR is scripts/fetch_alda.sh's directory. Run by run_on_ssp.sh;
# test_alda.py builds the same sources without CMake.
cmake_minimum_required(VERSION 3.15)
project(alda_tests C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
file(GLOB ALDA_CORE_SRC "${ALDA_DIR}/impl/*.c")
add_library(alda_core STATIC ${ALDA_CORE_SRC} ${ALDA_DIR}/scheduler.c ${SRC}/AldaCore.c)
target_include_directories(alda_core
        PUBLIC ${ALDA_DIR}/include ${SRC}/shim
        PRIVATE ${ALDA_DIR}/include/alda ${ALDA_DIR}/impl)
target_compile_options(alda_core PRIVATE -std=gnu11)

add_executable(alda_test alda_test.cpp ${SRC}/AldaEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/engine/ScriptEngine.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common/engine/Clock.cpp)
target_include_directories(alda_test PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/readerwriterqueue)
target_link_libraries(alda_test PRIVATE alda_core pthread m)

add_executable(alda_suite ${ALDA_DIR}/tests/test_shared_suite.c)
target_compile_options(alda_suite PRIVATE -std=gnu11)
target_link_libraries(alda_suite PRIVATE alda_core m)
