# Sources for plugins driven from a Launchpad Mini MK3 or X (Launchpad.h, LaunchpadDevice.h); include after
# common/CMakeLists.txt, which puts plugins/common on the include path.
set(LAUNCHPAD_SRC
        ${CMAKE_CURRENT_LIST_DIR}/Launchpad.cpp
        ${CMAKE_CURRENT_LIST_DIR}/LaunchpadDevice.cpp
)
