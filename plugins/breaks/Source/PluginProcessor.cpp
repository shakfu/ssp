#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

using brks::Panel;

static float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

// in Panel's K_ and B_ order
static const char* const KNOB_IDS[Panel::K_MAX] = { "break", "grimoire", "amen", "random", "sample" };

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<brks::BreaksEngine>()),
      volume_(*vts().getParameter("volume")),
      filter_(*vts().getParameter("filter")),
      gating_(*vts().getParameter("gating")),
      tempo_(*vts().getParameter("tempo")),
      clockOut_(*vts().getParameter("clock_out")),
      mode_(*vts().getParameter("mode")),
      tap_(*vts().getParameter("tap")),
      play_(*vts().getParameter("play")) {
    for (int k = 0; k < Panel::K_MAX; k++) knobs_[k] = vts().getParameter(KNOB_IDS[k]);
    brks().onIdle = [this] { syncTempo(); };
    init();
    if (File(DEFAULT_CARD).isDirectory()) loadCard(DEFAULT_CARD);
}

// The members syncTempo uses go before EngineProcessor stops the worker.
PluginProcessor::~PluginProcessor() {
    withIdleStopped([this] { brks().onIdle = nullptr; });
}

// Tempo follows the tempo playing (Tap, In 1, the card's), except for 1 s after it is turned or restored. A
// background write, as BaseProcessor's MIDI automation makes.
void PluginProcessor::syncTempo() {
    float v = value(tempo_);
    if (v != tempoSeen_) {
        if (v != tempoSet_) tempoHold_ = 1000 / IDLE_MS;
        tempoSeen_ = v;
    }
    if (tempoHold_ > 0) {
        tempoHold_--;
        return;
    }
    auto s = brks().status();
    if (!s.loaded || std::lround(v) == long(s.tempo)) return;
    tempoSet_ = tempoSeen_ = float(s.tempo);
    tempo_.beginChangeGesture();
    tempo_.setValueNotifyingHost(tempo_.convertTo0to1(tempoSet_));
    tempo_.endChangeGesture();
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    Panel d;
    const char* names[Panel::K_MAX] = { "Break", "Grimoire", "Amen", "Random", "Sample" };
    for (int k = 0; k < Panel::K_MAX; k++)
        params.add(std::make_unique<ssp::BaseFloatParameter>(KNOB_IDS[k], names[k], 0.0f, 1.0f, d.knobs[k]));
    params.add(std::make_unique<ssp::BaseFloatParameter>("volume", "Volume", 0.0f, 1.0f, d.volume));
    params.add(std::make_unique<ssp::BaseFloatParameter>("filter", "Filter", 0.0f, 1.0f, d.filter));
    params.add(std::make_unique<ssp::BaseFloatParameter>("gating", "Gating", 0.0f, 1.0f, d.gating));
    params.add(std::make_unique<ssp::BaseFloatParameter>("tempo", "Tempo", 30.0f, 300.0f, d.tempo, 1.0f));
    params.add(
        std::make_unique<ssp::BaseChoiceParameter>("clock_out", "Clock Out", StringArray(CLOCK_NAMES, 7), d.division));
    for (auto [id, name] : { std::pair{ "mode", "Mode" }, { "tap", "Tap" }, { "play", "Play" } })
        params.add(std::make_unique<ssp::BaseFloatParameter>(id, name, 0.0f, 1.0f, 0.0f, 1.0f));
    return params;
}

// one mono bus per jack, in the engine's I_ and O_ order
PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "Clock", "Tap", "Mode", "Mult", "Bank", "Play", "Unused 7", "Unused 8", "Amen CV", "Break CV",
                        "Sample CV" })
        props.addBus(true, name, AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R", "Trig", "Clock Out" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

// audio thread, before each Engine::process: parameters and patched CV to the engine
void PluginProcessor::control(const float* const*, int) {
    auto& p = brks().panel;
    for (int k = 0; k < Panel::K_MAX; k++) p.knobs[k] = value(*knobs_[k]);
    p.volume = value(volume_);
    p.filter = value(filter_);
    p.gating = value(gating_);
    p.division = uint8_t(std::lround(value(clockOut_)));
    p.tempo = uint16_t(std::lround(value(tempo_)));
    p.buttons[Panel::B_MODE] = value(mode_) > 0.5f;
    p.buttons[Panel::B_TAP] = value(tap_) > 0.5f;
    p.play = value(play_) > 0.5f;
    for (int c = 0; c < 3; c++) brks().cvPatched[c] = isInputEnabled(unsigned(brks::BreaksEngine::I_AMEN + c));
}

void PluginProcessor::loadCard(const String& path) {
    brks().load(path.toStdString());
}

void PluginProcessor::customToXml(XmlElement* xml) {
    auto s = brks().saved();
    xml->setAttribute("card", String(brks().folder()));
    if (!brks().status().loaded) return;  // no card playing: the card's own tempo and bank on the next load
    xml->setAttribute("tempo", int(s.tempo));
    xml->setAttribute("playing_bank", int(s.bank));
    xml->setAttribute("trigger_mode", int(s.triggerMode));
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    brks::Saved d, s;
    s.tempo = xml->hasAttribute("tempo") ? uint16_t(jlimit(30, 300, xml->getIntAttribute("tempo"))) : 0;
    s.bank = uint8_t(jlimit(0, 15, xml->getIntAttribute("playing_bank", d.bank)));
    s.triggerMode = uint8_t(jlimit(0, 3, xml->getIntAttribute("trigger_mode", d.triggerMode)));
    String card = xml->getStringAttribute("card");
    if (card.isEmpty()) return;
    brks().restore(s);
    brks().load(card.toStdString(), s.bank);
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using C = ssp::engine::ParamPage::Control;
    auto k = [this](int i) { return C{ knobs_[i], 0.05f, 0.01f }; };
    return { { "play", Colours::gold,
               { k(Panel::K_BREAK), k(Panel::K_GRIMOIRE), k(Panel::K_AMEN), k(Panel::K_RANDOM) } },
             { "sound", Colours::orange,
               { k(Panel::K_SAMPLE), C{ &volume_, 0.05f, 0.01f }, C{ &filter_, 0.05f, 0.01f },
                 C{ &gating_, 0.05f, 0.01f } } },
             { "clock", Colours::cyan, { C{ &tempo_, 5.0f, 1.0f }, C{ &clockOut_, 1.0f, 1.0f } } } };
}

// soft keys 1-3; 5 is Load. Left and Right step the bank (PluginEditor).
std::vector<RangedAudioParameter*> PluginProcessor::buttons() {
    return { &tap_, &mode_, &play_, nullptr, nullptr, nullptr, nullptr, nullptr };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), buttons()), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
