#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<fsyn::FluidEngine>()),
      gain_(*vts().getParameter("gain")),
      reverb_(*vts().getParameter("reverb")),
      chorus_(*vts().getParameter("chorus")),
      voices_(*vts().getParameter("voices")) {
    init();
    noteInput(true);
    if (File(DEFAULT_FONT).existsAsFile()) loadFont(DEFAULT_FONT);
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    fsyn::Settings d;
    params.add(std::make_unique<ssp::BaseFloatParameter>("gain", "Gain", 0.0f, 2.0f, d.gain));
    params.add(std::make_unique<ssp::BaseFloatParameter>("reverb", "Reverb", 0.0f, 1.0f, d.reverb));
    params.add(std::make_unique<ssp::BaseFloatParameter>("chorus", "Chorus", 0.0f, 1.0f, d.chorus));
    params.add(std::make_unique<ssp::BaseFloatParameter>("voices", "Voices", 1.0f, float(fsyn::FluidEngine::MAX_VOICES),
                                                         float(d.voices), 1.0f));
    return params;
}

// one mono bus per jack, in the engine's I_ and O_ order
PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    props.addBus(true, "Gain CV", AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

// audio thread, before each Engine::process: parameters to the engine
void PluginProcessor::control(const float* const*, int) {
    auto& s = fluid().settings;
    s.gain = value(gain_);
    s.reverb = value(reverb_);
    s.chorus = value(chorus_);
    s.voices = roundToInt(value(voices_));
}

// MIDI input thread: every channel message to the engine's queue
void PluginProcessor::midiMessageInput(const MidiMessage& m) {
    const uint8* d = m.getRawData();
    int n = m.getRawDataSize();
    if (n < 2 || n > 3) return;
    fluid().post({ d[0], d[1], uint8_t(n > 2 ? d[2] : 0) });
}

void PluginProcessor::loadFont(const String& path) {
    fluid().load(path.toStdString());
}

void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("soundfont", String(fluid().path()));
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    loadFont(xml->getStringAttribute("soundfont", DEFAULT_FONT));
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using C = ssp::engine::ParamPage::Control;
    return { { "synth", Colours::orange, { C{ &gain_, 0.05f, 0.01f }, C{ &reverb_, 0.05f, 0.01f },
                                           C{ &chorus_, 0.05f, 0.01f }, C{ &voices_, 8.0f, 1.0f } } } };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
