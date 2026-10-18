# engine_test, built for the host or, with xcSSP.cmake, for the SSP, against the FluidSynth that
# scripts/build_fluidsynth.sh built into DEPS_DIR. Run by run_on_ssp.sh; test_fsyn.py builds the same
# sources without CMake.
cmake_minimum_required(VERSION 3.15)
project(fsyn_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
add_executable(engine_test engine_test.cpp ${SRC}/FluidEngine.cpp)
target_include_directories(engine_test PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/readerwriterqueue ${DEPS_DIR}/include)
target_link_libraries(engine_test PRIVATE ${DEPS_DIR}/lib/libfluidsynth.a pthread m)
if (BUILDROOT)  # GCC 8's std::filesystem, which FluidSynth uses
    target_link_libraries(engine_test PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a)
endif ()
