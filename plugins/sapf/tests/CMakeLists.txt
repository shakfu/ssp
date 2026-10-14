# The step 1 harness (docs/dev/sapf-plugin.md): sapf-sc's interpreter, built for the host or, with
# xcSSP.cmake, for the SSP. SAPF_SC_DIR is a sapf-sc checkout, such as scripts/fetch_sapf.sh's.
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

# Step 2: the interpreter on a ScWorld, with SCSYNTH_DEPS a build_deps.sh prefix (build/deps/host)
if (SCSYNTH_DEPS)
    set(SCSY ${CMAKE_CURRENT_SOURCE_DIR}/../../scsynth/Source)
    set(SC ${SCSYNTH_DEPS}/include/supercollider)
    add_executable(sapf_server_test server_test.cpp ../Source/SapfServer.cpp ${SCSY}/ScWorld.cpp ${SCSY}/ScsyDef.cpp)
    target_include_directories(sapf_server_test PRIVATE ../Source ${SCSY})
    # scsynth's headers and defines only where they are used, as in plugins/scsynth/CMakeLists.txt
    set_source_files_properties(${SCSY}/ScWorld.cpp PROPERTIES
            COMPILE_DEFINITIONS "SC_AUDIO_API=SC_AUDIO_API_SSP;SC_AUDIO_API_SSP=100;SC_MEMORY_ALIGNMENT=32"
            INCLUDE_DIRECTORIES "${SC}/include/common;${SC}/common;${SC}/include/server;${SC}/include/plugin_interface;${SC}/server/scsynth;${SC}/external_libraries/boost;${SC}/external_libraries/boost_sync/include;${SC}/external_libraries/TLSF-2.4.6/src;${SCSYNTH_DEPS}/include")
    target_link_libraries(sapf_server_test PRIVATE sapf_engine ${SCSYNTH_DEPS}/lib/libscsynth.a
            ${SCSYNTH_DEPS}/lib/libsndfile.a dl m)
    if (TARGET_SSP)  # glibc 2.32 keeps shm_open in librt
        target_link_libraries(sapf_server_test PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a rt)
    endif ()

    # Step 4: the plugin's Session, TextBuffer and KeyQueue
    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
    add_executable(sapf_session_test session_test.cpp ${SRC}/Session.cpp ${SRC}/TextBuffer.cpp ${SRC}/KeyQueue.cpp
            ${SRC}/SapfServer.cpp ${SCSY}/ScWorld.cpp ${SCSY}/ScsyDef.cpp)
    target_include_directories(sapf_session_test PRIVATE ${SRC} ${SCSY} ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
    target_link_libraries(sapf_session_test PRIVATE sapf_engine ${SCSYNTH_DEPS}/lib/libscsynth.a
            ${SCSYNTH_DEPS}/lib/libsndfile.a dl m)
    if (TARGET_SSP)
        target_link_libraries(sapf_session_test PRIVATE ${BUILDROOT}/arm-rockchip-linux-gnueabihf/lib/libstdc++fs.a rt)
    endif ()
endif ()
