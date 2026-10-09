include_directories("${CMAKE_CURRENT_LIST_DIR}")

include_directories("${CMAKE_CURRENT_LIST_DIR}/../../external/readerwriterqueue")

set(COMMON_SRC
        ${CMAKE_CURRENT_LIST_DIR}/SSP.cpp
        ${CMAKE_CURRENT_LIST_DIR}/SSPApiEditor.cpp

        ${CMAKE_CURRENT_LIST_DIR}/ssp/BaseView.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/BaseProcessor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/SystemEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/SystemFullEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/SystemMiniEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/EditorHost.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/SSPUI.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/Log.cpp

        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/BaseEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/BaseViewEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/SingleViewEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/MultiViewEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/MultiBarEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/LineParamEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/TableParamEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/BarParamEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/FileBrowser.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/TextEdit.cpp

        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/BaseMiniView.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/MiniBasicView.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/MiniParamView.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/LineMiniEditor.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/editors/PageMiniView.cpp

        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/ParamControl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/ParamButton.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/ValueControl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/ValueButton.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/RmsTrack.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/VuMeter.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/MiniControl.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/ButtonBox.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/FileSelector.cpp
        ${CMAKE_CURRENT_LIST_DIR}/ssp/controls/TextControl.cpp


)
