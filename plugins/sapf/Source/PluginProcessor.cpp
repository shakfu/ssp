#include "PluginProcessor.h"

#include <chrono>
#include <cstdlib>

#include "PluginEditor.h"
#include "keys/KeyInput.h"
#include "ssp/EditorHost.h"

namespace {

// UGens in /media/BOOT/sapf/ugens, a copy of scsy's: scsy and sapf in one patch must not share the
// UGen libraries (docs/dev/sapf-plugin.md, Risks). $SAPF_UGENS and $SAPF_PRELUDE override.
std::string envOr(const char* name, const String& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback.toStdString();
}

}  // namespace

PluginProcessor::PluginProcessor()
    : BaseProcessor(getBusesProperties(), createParameterLayout()),
      session_(std::make_unique<sapf::Session>(sapf::Session::Paths{
          envOr("SAPF_UGENS", filesDir() + "/ugens"), envOr("SAPF_PRELUDE", filesDir() + "/sapf-prelude.txt"),
          filesDir().toStdString() })) {
    init();
    ssp::KeyInput::acquire(SAPF_KEY_PORT, SAPF_KEYBOARD_ENV);
    ssp::KeyInput::setTarget(&keys_);  // the newest instance takes the keys
}

PluginProcessor::~PluginProcessor() {
    ssp::KeyInput::clearTarget(&keys_);
    ssp::KeyInput::release();
    session_.reset();  // its threads end first
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    BaseProcessor::addBaseParameters(params);
    return params;
}

String PluginProcessor::filesDir() {
    const char* dir = std::getenv("SAPF_FILES");
    return dir && *dir ? String(dir) : String("/media/BOOT/sapf");
}

void PluginProcessor::prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) {
    BaseProcessor::prepareToPlay(newSampleRate, estimatedSamplesPerBlock);
    sampleRate_ = newSampleRate;
    maxBlock_ = std::max(1, estimatedSamplesPerBlock);
    in_.setSize(CHANNELS, maxBlock_);
    spare_.setSize(CHANNELS, maxBlock_);
    session_->prepare(float(newSampleRate));
}

void PluginProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    BaseProcessor::processBlock(buffer, midi);
    auto t0 = std::chrono::steady_clock::now();
    const int n = buffer.getNumSamples(), chans = buffer.getNumChannels();
    if (n == 0 || maxBlock_ == 0) return;
    if (chans < CHANNELS) in_.clear();  // a host with fewer channels leaves the rest silent
    for (int c = 0; c < CHANNELS; c++) inPtrs_[c] = in_.getReadPointer(c);
    // the host may exceed the block size it announced
    for (int pos = 0; pos < n; pos += maxBlock_) {
        const int len = std::min(maxBlock_, n - pos);
        for (int c = 0; c < std::min(chans, CHANNELS); c++) in_.copyFrom(c, 0, buffer, c, pos, len);
        for (int c = 0; c < CHANNELS; c++)
            outPtrs_[c] = c < chans ? buffer.getWritePointer(c, pos) : spare_.getWritePointer(c);
        session_->process(inPtrs_, outPtrs_, len);
    }
    for (int c = CHANNELS; c < chans; c++) buffer.clear(c, 0, n);
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    float avg = loadAvg_.load(std::memory_order_relaxed);
    loadAvg_.store(avg + 0.01f * (float(dt * sampleRate_ / n) - avg), std::memory_order_relaxed);
}

String PluginProcessor::save() {
    juce::File f(filesDir() + "/" + current_);
    f.getParentDirectory().createDirectory();
    return f.replaceWithText(text_.text() + "\n") ? "saved " + f.getFullPathName() : "cannot write " + f.getFullPathName();
}

String PluginProcessor::open(const String& name) {
    juce::File f(filesDir() + "/" + name);
    if (!f.existsAsFile()) return "no " + f.getFullPathName();
    text_.setText(f.loadFileAsString().trimEnd().toStdString());
    current_ = name;
    return "opened " + f.getFullPathName();
}

StringArray PluginProcessor::files() const {
    StringArray names;
    for (const auto& f : juce::File(filesDir()).findChildFiles(juce::File::findFiles, false, "*.sapf;*.txt"))
        names.add(f.getFileName());
    names.sort(true);
    return names;
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new PluginMiniEditor(*this), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

// the code, so a patch reopens with it
void PluginProcessor::customToXml(juce::XmlElement* xml) {
    xml->setAttribute("CODE", String(text_.text()));
    xml->setAttribute("FILE", current_);
}

void PluginProcessor::customFromXml(juce::XmlElement* xml) {
    if (xml->hasAttribute("CODE")) text_.setText(xml->getStringAttribute("CODE").toStdString());
    // a name only: a preset cannot point the code's saves outside filesDir()
    String name = xml->getStringAttribute("FILE", "live.sapf");
    if (name.isNotEmpty() && !name.containsAnyOf("/\\") && name != "." && name != "..") current_ = name;
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
