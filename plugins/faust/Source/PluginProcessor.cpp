#include "PluginProcessor.h"

#include "tree-sitter-faust-highlights.h"

extern "C" const TSLanguage* tree_sitter_faust(void);

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<fstr::FaustRuntime>("/media/BOOT/faust/libraries"), "/media/BOOT/faust",
                      { 7772, "FSTR_KEYBOARD", ".dsp", "//", tree_sitter_faust, FAUST_HIGHLIGHTS, true,
                        // no C-E: a Faust program compiles whole
                        "C-Enter run  C-L clear  C-S save  C-S-S as  C-O open  Esc pages" }) {
    noteInput(true);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
