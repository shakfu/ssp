#include "PluginProcessor.h"

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<mlsp::UseqEngine>(), "/media/BOOT/mlsp",
                      { 7777, "MLSP_KEYBOARD", ".useq", ";", nullptr, nullptr, false,
                        "C-Enter run  C-E form  C-. clear  C-R reset  C-S save  C-O open  Esc" },
                      getBusesProperties()) {
    applyDefaults();  // the program's control defaults; a preset restored later replaces them
    midiTransportInput(true);
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static_assert(mlsp::UseqEngine::GATES + mlsp::UseqEngine::CVS == CHANNELS, "a jack per output");
    BusesProperties props;
    for (int c = 0; c < CHANNELS; c++) props.addBus(true, "In " + String(c + 1), AudioChannelSet::mono());
    for (int k = 1; k <= mlsp::UseqEngine::GATES; k++) props.addBus(false, "d" + String(k), AudioChannelSet::mono());
    for (int k = 1; k <= mlsp::UseqEngine::CVS; k++) props.addBus(false, "a" + String(k), AudioChannelSet::mono());
    return props;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
