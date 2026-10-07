# Sources for plugins built on ssp::engine; include after ../common/CMakeLists.txt.
set(ENGINE_SRC
        ../common/engine/EngineProcessor.cpp
        ../common/engine/EngineEditor.cpp
)

# plugins built on ScriptProcessor add these too
set(SCRIPT_SRC
        ../common/engine/ScriptEngine.cpp
        ../common/engine/ScriptProcessor.cpp
)
