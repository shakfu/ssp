#include "PluginProcessor.h"

#include <cstdlib>

#include "PluginEditor.h"
#include "ssp/EditorHost.h"
#include "tree-sitter-sapf-highlights.h"

extern "C" const TSLanguage* tree_sitter_sapf(void);

namespace {

// $SAPF_PRELUDE overrides the prelude in filesDir()
std::string envOr(const char* name, const String& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback.toStdString();
}

// A block's peak for the meters; below METER_FLOOR (-40 dB, 50 mV) it reads 0, so an unplugged input's
// offset and noise, about -55 dB on the SSP, do not show as signal. The signal itself is untouched.
constexpr float METER_FLOOR = 0.01f;

float peak(const float* x, int n) {
    const auto r = juce::FloatVectorOperations::findMinAndMax(x, n);
    const float p = std::max(-r.getStart(), r.getEnd());
    return p < METER_FLOOR ? 0.0f : p;
}

}  // namespace

PluginProcessor::PluginProcessor()
    : ScriptProcessor(std::make_unique<sapf::SapfEngine>(sapf::Session::Paths{
                          envOr("SAPF_PRELUDE", filesDir() + "/sapf-prelude.txt"), filesDir().toStdString() }),
                      filesDir(),
                      { SAPF_KEY_PORT, SAPF_KEYBOARD_ENV, ".sapf", ";", tree_sitter_sapf, SAPF_HIGHLIGHTS, false,
                        "C-Enter run  C-E block  C-. stop  C-L clear  C-S save  C-O open  Esc", "word" }) {
    applyDefaults();  // the mouse in the middle; a preset restored later replaces it
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using sapf::SapfEngine;
    ssp::engine::ParamPage page;
    page.name = "mouse";
    page.c[0] = { &param(SapfEngine::MOUSE_X), 0.05f, 0.005f };
    page.c[1] = { &param(SapfEngine::MOUSE_Y), 0.05f, 0.005f };
    return { page };
}

String PluginProcessor::filesDir() {
    const char* dir = std::getenv("SAPF_FILES");
    return dir && *dir ? String(dir) : String("/media/BOOT/sapf");
}

void PluginProcessor::control(const float* const* in, int n) {
    ScriptProcessor::control(in, n);
    for (int c = 0; c < METERED; c++) inLevel_[c].process(peak(in[c], n));
}

void PluginProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midi) {
    ScriptProcessor::processBlock(buffer, midi);
    const int n = buffer.getNumSamples();
    for (int c = 0; c < std::min(METERED, buffer.getNumChannels()); c++)
        if (n > 0) outLevel_[c].process(peak(buffer.getReadPointer(c), n));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new PluginMiniEditor(*this), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

// 0.1.0 kept the text as CODE and its file's name as FILE, and ran nothing on load: the text comes back
// unrun, and Ctrl-S still writes that file
void PluginProcessor::customFromXml(juce::XmlElement* xml) {
    if (xml->hasAttribute("CODE") && !xml->hasAttribute("program")) {
        // a name only: a preset cannot point the code's saves outside filesDir()
        String name = xml->getStringAttribute("FILE", "live.sapf");
        if (name.isEmpty() || name.containsAnyOf("/\\") || name == "." || name == "..") name = "live.sapf";
        xml->setAttribute("program", filesDir() + "/" + name);
        xml->setAttribute("code", "");
        xml->setAttribute("text", xml->getStringAttribute("CODE"));
    }
    ScriptProcessor::customFromXml(xml);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
