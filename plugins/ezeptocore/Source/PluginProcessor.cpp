#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

using ezep::Panel;

static float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

// in Panel's K_ and B_ order
static const char* const KNOB_IDS[Panel::K_MAX] = { "break", "grimoire", "amen", "random", "sample" };
static const char* const BUTTON_IDS[Panel::B_MAX] = { "mode", "mult", "bank", "tap" };

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<ezep::EzepEngine>()),
      volume_(*vts().getParameter("volume")),
      filter_(*vts().getParameter("filter")),
      gating_(*vts().getParameter("gating")),
      play_(*vts().getParameter("play")) {
    for (int k = 0; k < Panel::K_MAX; k++) knobs_[k] = vts().getParameter(KNOB_IDS[k]);
    for (int b = 0; b < Panel::B_MAX; b++) buttons_[b] = vts().getParameter(BUTTON_IDS[b]);
    init();
    if (File(DEFAULT_CARD).isDirectory()) loadCard(DEFAULT_CARD);
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
    const char* buttons[Panel::B_MAX] = { "Mode", "Mult", "Bank", "Tap" };
    for (int b = 0; b < Panel::B_MAX; b++)
        params.add(std::make_unique<ssp::BaseFloatParameter>(BUTTON_IDS[b], buttons[b], 0.0f, 1.0f, 0.0f, 1.0f));
    params.add(std::make_unique<ssp::BaseFloatParameter>("play", "Play", 0.0f, 1.0f, 0.0f, 1.0f));
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
    auto& p = ezep().panel;
    for (int k = 0; k < Panel::K_MAX; k++) p.knobs[k] = value(*knobs_[k]);
    p.volume = value(volume_);
    p.filter = value(filter_);
    p.gating = value(gating_);
    for (int b = 0; b < Panel::B_MAX; b++) p.buttons[b] = value(*buttons_[b]) > 0.5f;
    p.play = value(play_) > 0.5f;
    for (int c = 0; c < 3; c++) ezep().cvPatched[c] = isInputEnabled(unsigned(ezep::EzepEngine::I_AMEN + c));
}

void PluginProcessor::loadCard(const String& path) {
    ezep().load(path.toStdString());
}

void PluginProcessor::customToXml(XmlElement* xml) {
    auto s = ezep().saved();
    xml->setAttribute("card", String(ezep().folder()));
    xml->setAttribute("tempo", int(s.tempo));
    xml->setAttribute("playing_bank", int(s.bank));
    xml->setAttribute("division", int(s.division));
    xml->setAttribute("trigger_mode", int(s.triggerMode));
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    ezep::Saved d, s;
    s.tempo = uint16_t(jlimit(30, 300, xml->getIntAttribute("tempo", d.tempo)));
    s.bank = uint8_t(jlimit(0, 15, xml->getIntAttribute("playing_bank", d.bank)));
    s.division = uint8_t(jlimit(0, 6, xml->getIntAttribute("division", d.division)));
    s.triggerMode = uint8_t(jlimit(0, 3, xml->getIntAttribute("trigger_mode", d.triggerMode)));
    String card = xml->getStringAttribute("card");
    if (card.isEmpty()) return;
    ezep().restore(s);
    ezep().load(card.toStdString(), s.bank);
}

std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    using C = ssp::engine::ParamPage::Control;
    auto k = [this](int i) { return C{ knobs_[i], 0.05f, 0.01f }; };
    return { { "play", Colours::gold,
               { k(Panel::K_BREAK), k(Panel::K_AMEN), k(Panel::K_SAMPLE), k(Panel::K_RANDOM) } },
             { "tap", Colours::orange,
               { k(Panel::K_GRIMOIRE), C{ &volume_, 0.05f, 0.01f }, C{ &filter_, 0.05f, 0.01f },
                 C{ &gating_, 0.05f, 0.01f } } } };
}

// soft keys 1-4 and 6; 5 is Load
std::vector<RangedAudioParameter*> PluginProcessor::buttons() {
    return { buttons_[Panel::B_TAP], buttons_[Panel::B_MODE], buttons_[Panel::B_MULT], buttons_[Panel::B_BANK],
             nullptr, &play_, nullptr, nullptr };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), buttons()), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
