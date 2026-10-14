# Sources for plugins that read the USB keyboard and a TCP key port (KeyInput.h); include after
# common/CMakeLists.txt, which puts plugins/common on the include path.
set(KEYS_SRC
        ${CMAKE_CURRENT_LIST_DIR}/KeyInput.cpp
)
