#include "PluginProcessor.h"

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<fstr::FaustRuntime>("/media/BOOT/faust/libraries"), "/media/BOOT/faust",
                      { 7772, "FSTR_KEYBOARD", ".dsp" }) {
    noteInput(true);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
