# prdt_test: PdEngine against the host build of libpd; configured and run by test_prdt.py
cmake_minimum_required(VERSION 3.25)
project(prdt_test C CXX)

set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
add_subdirectory(../pd pd)
find_package(Threads REQUIRED)

add_executable(prdt_test prdt_test.cpp ../Source/PdEngine.cpp ${ROOT}/plugins/common/engine/ScriptEngine.cpp)
target_compile_features(prdt_test PRIVATE cxx_std_17)
target_include_directories(prdt_test PRIVATE ../Source ${ROOT}/plugins/common ${ROOT}/external/readerwriterqueue)
target_link_libraries(prdt_test PRIVATE pd_extra pd_cyclone pd_else Threads::Threads m ${CMAKE_DL_LIBS})
target_compile_definitions(prdt_test PRIVATE EXAMPLES="${CMAKE_CURRENT_LIST_DIR}/../examples"
        EXTRA="${ROOT}/external/libpd/pure-data/extra" ELSE_ABSTRACTIONS="${ROOT}/external/pd-else/Abstractions")
