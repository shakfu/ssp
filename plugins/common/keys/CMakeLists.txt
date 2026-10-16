# Sources for plugins that read the USB keyboard and a TCP key port (KeyInput.h); include after
# common/CMakeLists.txt, which puts plugins/common on the include path.
set(KEYS_SRC
        ${CMAKE_CURRENT_LIST_DIR}/KeyInput.cpp
)

# plugins that edit code from those keys add this too
set(TEXT_SRC
        ${CMAKE_CURRENT_LIST_DIR}/CodeText.cpp
        ${CMAKE_CURRENT_LIST_DIR}/KeyQueue.cpp
        ${CMAKE_CURRENT_LIST_DIR}/TextBuffer.cpp
)
