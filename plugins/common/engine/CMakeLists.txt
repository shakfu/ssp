# Sources for plugins built on ssp::engine; include after ../common/CMakeLists.txt.
set(ENGINE_SRC
        ../common/engine/EngineProcessor.cpp
        ../common/engine/EngineEditor.cpp
)

# plugins that stream files (radio, pstretch, bard) add this too
set(STREAM_SRC
        ../common/engine/Station.cpp
)

# plugins built on FaustProcessor add this too
set(FAUST_SRC
        ../common/engine/FaustProcessor.cpp
)

# plugins built on ScriptProcessor add these too
set(SCRIPT_SRC
        ../common/engine/ScriptEngine.cpp
        ../common/engine/ScriptProcessor.cpp
)
