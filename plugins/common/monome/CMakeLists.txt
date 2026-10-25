# Sources for plugins driven from a monome grid (Grid.h, GridDevice.h); include after common/CMakeLists.txt.
# GridDevice links libmonome, libftdi1 and libusb from scripts/build_monome.sh: ssp_link_monome(target) adds them
# and defines SSP_MONOME=1 where they are built for this target (DEPS_DIR), else nothing.
set(MONOME_SRC ${CMAKE_CURRENT_LIST_DIR}/Grid.cpp)

function(ssp_link_monome target)
    if (NOT EXISTS "${DEPS_DIR}/lib/libmonome.a")
        message(STATUS "${target}: no monome grid: run scripts/build_monome.sh")
        return()
    endif ()
    find_package(Threads REQUIRED)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GridDevice.cpp)
    target_include_directories(${target} PRIVATE ${DEPS_DIR}/include ${DEPS_DIR}/include/libftdi1)
    target_link_libraries(${target} PRIVATE ${DEPS_DIR}/lib/libmonome.a ${DEPS_DIR}/lib/libftdi1.a
            ${DEPS_DIR}/lib/libusb-1.0.a Threads::Threads)
    target_compile_definitions(${target} PRIVATE SSP_MONOME=1)
endfunction()
