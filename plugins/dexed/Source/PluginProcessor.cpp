#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

static int intValue(const RangedAudioParameter& p) {
    return roundToInt(value(p));
}

static String opId(int op) {
    return "op" + String(op + 1);
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<dexed::DexedEngine>()),
      voice_(*vts().getParameter("voice")),
      voices_(*vts().getParameter("voices")),
      level_(*vts().getParameter("level")),
      transpose_(*vts().getParameter("transpose")),
      cutoff_(*vts().getParameter("cutoff")),
      resonance_(*vts().getParameter("resonance")),
      algorithm_(*vts().getParameter("algorithm")),
      feedback_(*vts().getParameter("feedback")),
      lfoRate_(*vts().getParameter("lfo_rate")),
      lfoDepth_(*vts().getParameter("lfo_depth")),
      bendRange_(*vts().getParameter("bend_range")),
      portamento_(*vts().getParameter("portamento")),
      mono_(*vts().getParameter("mono")),
      engineType_(*vts().getParameter("engine")) {
    for (int op = 0; op < 6; op++) opLevel_[op] = vts().getParameter(opId(op));
    init();
    noteInput(true);
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    auto whole = [&](const String& id, const String& name, int lo, int hi, int def) {
        params.add(std::make_unique<ssp::BaseFloatParameter>(id, name, float(lo), float(hi), float(def), 1.0f));
    };
    whole("voice", "Voice", 1, dexed::Cartridge::VOICES, 1);
    whole("voices", "Voices", 1, dexed::DexedEngine::MAX_VOICES, dexed::DexedEngine::MAX_VOICES);
    params.add(std::make_unique<ssp::BaseFloatParameter>("level", "Level", 0.0f, 2.0f, 1.0f));
    whole("transpose", "Transpose", -24, 24, 0);
    params.add(std::make_unique<ssp::BaseFloatParameter>("cutoff", "Cutoff", 0.0f, 1.0f, 1.0f));
    params.add(std::make_unique<ssp::BaseFloatParameter>("resonance", "Resonance", 0.0f, 1.0f, 0.0f));
    StringArray algorithms{ "voice" };
    for (int a = 1; a <= 32; a++) algorithms.add(String(a));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("algorithm", "Algorithm", algorithms, 0));
    whole("feedback", "Feedback", -7, 7, 0);
    for (int op = 0; op < 6; op++) whole(opId(op), "Op " + String(op + 1), -99, 99, 0);
    whole("lfo_rate", "LFO Rate", -99, 99, 0);
    whole("lfo_depth", "LFO Depth", -99, 99, 0);
    whole("bend_range", "Bend Range", 0, 12, 2);
    whole("portamento", "Portamento", 0, 99, 0);
    params.add(std::make_unique<ssp::BaseBoolParameter>("mono", "Mono", false));
    // Synth_Dexed's MSFA, MKI and OPL; Mark I is its default
    params.add(std::make_unique<ssp::BaseChoiceParameter>("engine", "Engine", StringArray{ "modern", "mark I", "OPL" }, 1));
    return params;
}

// one mono bus per jack, in the engine's I_ and O_ order
PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "Cutoff CV", "Mod CV" }) props.addBus(true, name, AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

// audio thread, before each Engine::process: parameters to the engine
void PluginProcessor::control(const float* const*, int) {
    auto& s = dexed().settings;
    s.voice = intValue(voice_) - 1;
    s.voices = intValue(voices_);
    s.level = value(level_);
    s.transpose = intValue(transpose_);
    s.cutoff = value(cutoff_);
    s.resonance = value(resonance_);
    s.algorithm = intValue(algorithm_);
    s.feedback = intValue(feedback_);
    for (int op = 0; op < 6; op++) s.opLevel[op] = intValue(*opLevel_[op]);
    s.lfoRate = intValue(lfoRate_);
    s.lfoDepth = intValue(lfoDepth_);
    s.bendRange = intValue(bendRange_);
    s.portamento = intValue(portamento_);
    s.mono = mono_.getValue() > 0.5f;
    s.engine = intValue(engineType_);
}

// MIDI input thread. A program change selects the voice through its parameter, so the screen and
// the preset follow; the rest goes to the engine's queue.
void PluginProcessor::midiMessageInput(const MidiMessage& m) {
    if (m.isProgramChange()) {
        int v = dexed::programVoice(m.getProgramChangeNumber());
        if (v >= 0) voice_.setValueNotifyingHost(voice_.convertTo0to1(float(v + 1)));
        return;
    }
    const uint8* d = m.getRawData();
    int n = m.getRawDataSize();
    if (n < 2 || n > 3) return;
    dexed().post({ d[0], d[1], uint8_t(n > 2 ? d[2] : 0) });
}

int PluginProcessor::voice() const {
    return jlimit(0, dexed::Cartridge::VOICES - 1, intValue(voice_) - 1);
}

void PluginProcessor::stepVoice(int dir) {
    int v = (voice() + dir + dexed::Cartridge::VOICES) % dexed::Cartridge::VOICES;
    voice_.setValueNotifyingHost(voice_.convertTo0to1(float(v + 1)));
}

String PluginProcessor::voiceName(int v) const {
    return hasCart_ ? String(cart_.name(v)) : String("INIT VOICE");
}

void PluginProcessor::useCartridge(const dexed::Cartridge& c) {
    cart_ = c;
    hasCart_ = true;
    dexed().setCartridge(std::make_unique<dexed::Cartridge>(c));
}

void PluginProcessor::loadCartridge(const String& path) {
    dexed::Cartridge c;
    auto err = c.load(path.toStdString());
    if (!err.empty()) {
        status_ = File(path).getFileName() + ": " + String(err);  // the cartridge playing stays
        return;
    }
    useCartridge(c);
    path_ = path;
    status_ = c.checksumOk ? String() : String("checksum mismatch; loaded anyway");
}

// the selected voice's data too, so the preset still plays it if the file has gone
void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("cartridge", path_);
    if (hasCart_) xml->setAttribute("voice_data", String::toHexString(cart_.voice[voice()], dexed::Cartridge::VOICE_BYTES, 0));
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    String path = xml->getStringAttribute("cartridge");
    status_ = {};
    if (path.isEmpty()) {
        hasCart_ = false;
        path_ = {};
        dexed().setCartridge(nullptr);
        return;
    }
    loadCartridge(path);
    if (path_ == path) return;
    MemoryBlock data;
    data.loadFromHexString(xml->getStringAttribute("voice_data"));
    if (data.getSize() != size_t(dexed::Cartridge::VOICE_BYTES)) return;  // keeps what plays, with the error
    dexed::Cartridge c;
    for (auto& v : c.voice) {
        std::memcpy(v, data.getData(), sizeof v);
        dexed::Cartridge::sanitize(v);  // a preset is outside data too
    }
    useCartridge(c);
    path_ = path;
    status_ = File(path).getFileName() + " missing: the preset's voice in every slot";
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using C = ssp::engine::ParamPage::Control;
    auto ops = Colour(120, 200, 230);
    return {
        { "voice", Colours::orange, { C{ &voice_, 1.0f, 1.0f }, C{ &level_, 0.05f, 0.01f }, C{ &transpose_, 1.0f, 1.0f },
                                      C{ &voices_, 1.0f, 1.0f } } },
        { "tone", Colours::orange, { C{ &cutoff_, 0.02f, 0.005f }, C{ &resonance_, 0.02f, 0.005f },
                                     C{ &algorithm_, 1.0f, 1.0f }, C{ &feedback_, 1.0f, 1.0f } } },
        { "op 1-4", ops, { C{ opLevel_[0], 1.0f, 1.0f }, C{ opLevel_[1], 1.0f, 1.0f }, C{ opLevel_[2], 1.0f, 1.0f },
                           C{ opLevel_[3], 1.0f, 1.0f } } },
        { "op 5-6 lfo", ops, { C{ opLevel_[4], 1.0f, 1.0f }, C{ opLevel_[5], 1.0f, 1.0f }, C{ &lfoRate_, 1.0f, 1.0f },
                               C{ &lfoDepth_, 1.0f, 1.0f } } },
        { "play", Colours::white, { C{ &bendRange_, 1.0f, 1.0f }, C{ &portamento_, 1.0f, 1.0f }, C{ &mono_, 1.0f, 1.0f },
                                    C{ &engineType_, 1.0f, 1.0f } } },
    };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
