# Sources for plugins built on ssp::engine; include after common/CMakeLists.txt.
set(ENGINE_SRC
        ${CMAKE_CURRENT_LIST_DIR}/EngineProcessor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/EngineEditor.cpp
)

# sequencers (alda) add this too: ssp::engine::Clock, tempo or clock input or MIDI clock
set(CLOCK_SRC
        ${CMAKE_CURRENT_LIST_DIR}/Clock.cpp
)

# plugins that stream files (radio, pstretch, bard) add this too
set(STREAM_SRC
        ${CMAKE_CURRENT_LIST_DIR}/Station.cpp
)

# plugins built on FaustProcessor add this too
set(FAUST_SRC
        ${CMAKE_CURRENT_LIST_DIR}/FaustProcessor.cpp
)

# plugins built on ScriptProcessor add these too; they include its code editor, which a plugin colours by
# adding a tree-sitter grammar's parser.c (external/tree-sitter-<language>)
include(${CMAKE_CURRENT_LIST_DIR}/../keys/CMakeLists.txt)
set(SCRIPT_SRC
        ${CMAKE_CURRENT_LIST_DIR}/ScriptEngine.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ScriptProcessor.cpp
        ${KEYS_SRC}
        ${TEXT_SRC}
        ${HIGHLIGHT_SRC}
)
