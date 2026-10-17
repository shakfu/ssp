#include "PluginProcessor.h"

#include "tree-sitter-faust-highlights.h"

extern "C" const TSLanguage* tree_sitter_faust(void);

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<fstr::FaustRuntime>("/media/BOOT/faust/libraries"), "/media/BOOT/faust",
                      { 7772, "FSTR_KEYBOARD", ".dsp", "//", tree_sitter_faust, FAUST_HIGHLIGHTS, true }) {
    noteInput(true);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
