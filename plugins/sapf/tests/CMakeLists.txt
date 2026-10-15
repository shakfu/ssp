# The step 1 harness and the plugin's Session (docs/dev/sapf-plugin.md): the sapf fork's engine, built
# for the host or, with xcSSP.cmake, for the SSP. SAPF_DIR is a checkout of github.com/shakfu/sapf, such
# as scripts/fetch_sapf.sh's; DEPS a scripts/build_deps.sh prefix, for libsndfile.
cmake_minimum_required(VERSION 3.15)
project(sapf_harness C CXX)  # C: sapf's FFTW

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# the deps' libsndfile on both machines, as the plugin links it: the buildroot's .a is not PIC
add_library(SndFile::sndfile STATIC IMPORTED)
set_target_properties(SndFile::sndfile PROPERTIES IMPORTED_LOCATION "${DEPS}/lib/libsndfile.a"
        INTERFACE_INCLUDE_DIRECTORIES "${DEPS}/include")
set(SAPF_ENGINE_ONLY ON)
add_subdirectory(${SAPF_DIR} sapf)

# gcc 8 keeps std::filesystem in libstdc++fs
if (TARGET_SSP)
    set(STDCXXFS ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a)
endif ()

add_executable(sapf_harness harness.cpp)
target_link_libraries(sapf_harness PRIVATE sapf_engine ${STDCXXFS})

# Step 3: the plugin's Session, TextBuffer, KeyQueue and FilePicker
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
add_executable(sapf_session_test session_test.cpp ${SRC}/Session.cpp ${SRC}/TextBuffer.cpp ${SRC}/KeyQueue.cpp
        ${SRC}/FilePicker.cpp)
target_include_directories(sapf_session_test PRIVATE ${SRC} ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_link_libraries(sapf_session_test PRIVATE sapf_engine ${STDCXXFS})
