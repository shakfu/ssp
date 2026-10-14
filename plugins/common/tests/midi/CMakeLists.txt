# Host build of BaseProcessor's MIDI dispatch; run by ../test_base_midi.py.
set(COMMON "${CMAKE_CURRENT_LIST_DIR}/../..")

juce_add_console_app(base_midi_test VERSION 0.0.0 PRODUCT_NAME "base_midi_test")
target_sources(base_midi_test PRIVATE
        base_midi_test.cpp
        ${COMMON}/ssp/BaseProcessor.cpp
        ${COMMON}/ssp/Log.cpp)
target_include_directories(base_midi_test PRIVATE ${COMMON})
target_compile_definitions(base_midi_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JucePlugin_Name="base_midi_test")
target_link_libraries(base_midi_test PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_recommended_config_flags)
