# The step 1 harness (docs/dev/sapf-plugin.md): sapf-sc's interpreter, built for the host or, with
# xcSSP.cmake, for the SSP. SAPF_SC_DIR is a sapf-sc checkout.
cmake_minimum_required(VERSION 3.15)
project(sapf_harness CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

set(SAPF_ROOT ${SAPF_SC_DIR})
add_subdirectory(${SAPF_SC_DIR}/src/engine sapf_engine)

add_executable(sapf_harness harness.cpp)
target_link_libraries(sapf_harness PRIVATE sapf_engine)
# gcc 8 keeps std::filesystem in libstdc++fs
if (TARGET_SSP)
    target_link_libraries(sapf_harness PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a)
endif ()
