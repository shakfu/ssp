#include "PluginProcessor.h"

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<csnd::CsoundEngine>(), "/media/BOOT/csound") {
    noteInput(true);
}

void PluginProcessor::midiNoteInput(unsigned note, unsigned velocity) {
    auto& cs = static_cast<csnd::CsoundEngine&>(script());
    cs.midi(velocity > 0 ? 0x90 : 0x80, uint8_t(note & 0x7F), uint8_t(velocity & 0x7F));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
