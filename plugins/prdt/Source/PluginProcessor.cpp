#include "PluginProcessor.h"

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<prdt::PdEngine>(
                          // BOOT/prdt itself, so that [else/name] finds BOOT/prdt/else/name.pd
                          std::vector<std::string>{ "/media/BOOT/prdt/lib", "/media/BOOT/prdt/extra",
                                                    "/media/BOOT/prdt/cyclone", "/media/BOOT/prdt/else",
                                                    "/media/BOOT/prdt" }),
                      "/media/BOOT/prdt") {
    noteInput(true);
}

void PluginProcessor::midiMessageInput(const MidiMessage& m) {
    const uint8* raw = m.getRawData();
    const int size = m.getRawDataSize();
    if (size < 2 || raw[0] < 0x80 || raw[0] >= 0xF0) return;
    static_cast<prdt::PdEngine&>(script()).midi(raw[0], raw[1], size > 2 ? raw[2] : 0);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
