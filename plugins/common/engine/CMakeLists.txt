# Sources for plugins built on ssp::engine; include after common/CMakeLists.txt.
set(ENGINE_SRC
        ${CMAKE_CURRENT_LIST_DIR}/EngineProcessor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/EngineEditor.cpp
)

# plugins that stream files (radio, pstretch, bard) add this too
set(STREAM_SRC
        ${CMAKE_CURRENT_LIST_DIR}/Station.cpp
)

# plugins built on FaustProcessor add this too
set(FAUST_SRC
        ${CMAKE_CURRENT_LIST_DIR}/FaustProcessor.cpp
)

# plugins built on ScriptProcessor add these too
set(SCRIPT_SRC
        ${CMAKE_CURRENT_LIST_DIR}/ScriptEngine.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ScriptProcessor.cpp
)
