#include "PluginProcessor.h"

#include <cstdlib>

#include "keys/KeyInput.h"
#include "PluginEditor.h"
#include "ssp/Log.h"

// the firmware library, embedded by firmware_image.S
extern "C" const unsigned char ttyp_firmware_begin[];
extern "C" const unsigned char ttyp_firmware_end[];

namespace {

constexpr float SIGNAL_PER_VOLT = 0.2f;  // SSP: 1.0 is 5 V
// teletype's trigger input thresholds, as monome-rack models them
constexpr float TRIG_HIGH = 2.21f * SIGNAL_PER_VOLT;
constexpr float TRIG_LOW = 0.8f * SIGNAL_PER_VOLT;
constexpr size_t MAX_STATE = 4 << 20;  // decompressed preset state; the firmware's is 358 KB

}  // namespace

PluginProcessor::PluginProcessor() : PluginProcessor(getBusesProperties(), createParameterLayout()) {
}

PluginProcessor::PluginProcessor(const AudioProcessor::BusesProperties& ioLayouts,
                                 AudioProcessorValueTreeState::ParameterLayout layout)
    : BaseProcessor(ioLayouts, std::move(layout)),
      param_(*vts().getParameter(ID::param)),
      inRange_(*vts().getParameter(ID::in_range)) {
    for (unsigned o = 0; o < ttyp::Teletype::OUTS; o++) cvRange_[o] = vts().getParameter(ID::cvRange(o));
    init();
    teletype_ = std::make_unique<ttyp::Teletype>(ttyp_firmware_begin, size_t(ttyp_firmware_end - ttyp_firmware_begin));
    ssp::KeyInput::acquire(ttyp::KEY_PORT, ttyp::KEYBOARD_ENV);
    if (teletype_->ok()) {
        teletype_->start();
        ssp::KeyInput::setTarget(teletype_.get());  // the newest instance takes the keys
    }
}

PluginProcessor::~PluginProcessor() {
    ssp::KeyInput::clearTarget(teletype_.get());
    ssp::KeyInput::release();
    teletype_.reset();
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    BaseProcessor::addBaseParameters(params);
    params.add(std::make_unique<ssp::BaseFloatParameter>(ID::param, "Param", 0.0f, 1.0f, 0.5f));
    const juce::StringArray ranges{ "0-5V", "-5-5V" };
    for (unsigned o = 0; o < ttyp::Teletype::OUTS; o++)
        params.add(std::make_unique<ssp::BaseChoiceParameter>(ID::cvRange(o), "CV" + juce::String(o + 1) + " Range",
                                                              ranges, R_VOLTS));
    params.add(std::make_unique<ssp::BaseChoiceParameter>(ID::in_range, "In Range", ranges, R_VOLTS));
    return params;
}

const String PluginProcessor::getInputBusName(int channelIndex) {
    if (channelIndex < I_IN) return "Trig " + String(channelIndex - I_TRIG_1 + 1);
    if (channelIndex == I_IN) return "In";
    return "ZZIn-" + String(channelIndex);
}

const String PluginProcessor::getOutputBusName(int channelIndex) {
    if (channelIndex < O_CV_1) return "TR " + String(channelIndex - O_TR_1 + 1);
    if (channelIndex < O_MAX) return "CV " + String(channelIndex - O_CV_1 + 1);
    return "ZZOut-" + String(channelIndex);
}

void PluginProcessor::prepareToPlay(double newSampleRate, int estimatedSamplesPerBlock) {
    BaseProcessor::prepareToPlay(newSampleRate, estimatedSamplesPerBlock);
}

void PluginProcessor::processBlock(AudioSampleBuffer& buffer, MidiBuffer& midi) {
    auto* tt = teletype();
    const int n = buffer.getNumSamples();
    if (!tt || n == 0 || buffer.getNumChannels() < I_MAX) {
        buffer.clear();
        return;
    }
    auto shifted = [](juce::RangedAudioParameter& r) { return int(r.convertFrom0to1(r.getValue())) == R_SHIFT; };

    // inputs are read before the outputs overwrite their shared channels
    for (unsigned i = 0; i < ttyp::Teletype::TRIGS; i++) {
        if (!isInputEnabled(I_TRIG_1 + i)) {
            if (trigHigh_[i]) tt->setTrigger(i, trigHigh_[i] = false);
            continue;
        }
        const float* in = buffer.getReadPointer(I_TRIG_1 + i);
        for (int s = 0; s < n; s++) {
            bool high = trigHigh_[i] ? in[s] > TRIG_LOW : in[s] > TRIG_HIGH;
            if (high != trigHigh_[i]) tt->setTrigger(i, trigHigh_[i] = high);
        }
    }
    float in = isInputEnabled(I_IN) ? buffer.getSample(I_IN, n - 1) / SIGNAL_PER_VOLT : 0.0f;
    tt->setIn(shifted(inRange_) ? in + 5.0f : in);
    tt->setParam(param_.getValue());

    // the firmware's outputs change once per 1 ms tick, so one value per block
    for (unsigned o = 0; o < ttyp::Teletype::OUTS; o++) {
        float v = tt->cv(o);
        float cv = juce::jlimit(-1.0f, 1.0f, (shifted(*cvRange_[o]) ? v - 5.0f : v) * SIGNAL_PER_VOLT);
        float tr = tt->tr(o) ? 1.0f : 0.0f;
        juce::FloatVectorOperations::fill(buffer.getWritePointer(O_CV_1 + o), cv, n);
        juce::FloatVectorOperations::fill(buffer.getWritePointer(O_TR_1 + o), tr, n);
    }
    for (int c = O_MAX; c < buffer.getNumChannels(); c++) buffer.clear(c, 0, n);
}

String PluginProcessor::sceneDir() {
    const char* dir = std::getenv("TTYP_SCENES");
    return dir && *dir ? String(dir) : String("/media/BOOT/teletype");
}

String PluginProcessor::syncScenes() {
    auto* tt = teletype();
    if (!tt) return "no firmware";
    auto r = tt->syncDisk(sceneDir().toStdString());
    String msg = "disk: wrote " + String(r.written) + ", read " + String(r.read);
    return r.error.empty() ? msg : msg + "; " + String(r.error);
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new PluginMiniEditor(*this), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

// The scenes and the live scene, gzipped: mostly empty scene slots, so it compresses well.
void PluginProcessor::customToXml(juce::XmlElement* xml) {
    auto* tt = teletype();
    if (!tt) return;
    auto state = tt->saveState();
    juce::MemoryOutputStream zipped;
    {
        juce::GZIPCompressorOutputStream gz(zipped, 9);
        gz.write(state.data(), state.size());
    }
    xml->setAttribute("TELETYPE", zipped.getMemoryBlock().toBase64Encoding());
}

void PluginProcessor::customFromXml(juce::XmlElement* xml) {
    auto* tt = teletype();
    if (!tt || !xml->hasAttribute("TELETYPE")) return;
    juce::MemoryBlock zipped;
    if (!zipped.fromBase64Encoding(xml->getStringAttribute("TELETYPE"))) return;
    juce::MemoryInputStream raw(zipped, false);
    juce::GZIPDecompressorInputStream gz(raw);
    juce::MemoryBlock state;
    char chunk[16384];
    while (!gz.isExhausted() && state.getSize() <= MAX_STATE) {
        int got = gz.read(chunk, sizeof chunk);
        if (got <= 0) break;
        state.append(chunk, size_t(got));
    }
    // a size the firmware does not expect, or past the cap, is refused by loadState
    if (state.getSize() > MAX_STATE ||
        !tt->loadState(static_cast<const uint8_t*>(state.getData()), state.getSize()))
        ssp::log("ttyp: preset state refused");
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
