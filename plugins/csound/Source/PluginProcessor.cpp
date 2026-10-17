#include "PluginProcessor.h"

#include "tree-sitter-csound-highlights.h"

extern "C" const TSLanguage* tree_sitter_csound(void);

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<csnd::CsoundEngine>(), "/media/BOOT/csound",
                      { 7773, "CSND_KEYBOARD", ".csd", ";", tree_sitter_csound, CSOUND_HIGHLIGHTS }) {
    noteInput(true);
}

void PluginProcessor::midiNoteInput(unsigned note, unsigned velocity) {
    auto& cs = static_cast<csnd::CsoundEngine&>(script());
    cs.midi(velocity > 0 ? 0x90 : 0x80, uint8_t(note & 0x7F), uint8_t(velocity & 0x7F));
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
