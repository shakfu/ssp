#include "PluginProcessor.h"

#include <cstdlib>

// SynthDefs in /media/BOOT/scsy; the UGen plugins they use in its ugens folder, or in $SCSY_UGENS
// (for tests and desktop builds)
static std::string ugenDir() {
    const char* dir = std::getenv("SCSY_UGENS");
    return dir && *dir ? dir : "/media/BOOT/scsy/ugens";
}

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<scsy::ScsynthEngine>(ugenDir()), "/media/BOOT/scsy") {}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
