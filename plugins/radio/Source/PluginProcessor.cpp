#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static const char* DEFAULT_ROOT = "/media/BOOT/radio";
static const int RAW_RATES[] = { 22050, 32000, 44100, 48000 };
static const char* deckName[] = { "A", "B" };

static String deckId(int d, const char* id) {
    return String(deckName[d]).toLowerCase() + ":" + id;
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<radio::RadioEngine>()),
      crossfade_(*vts().getParameter("xfade")),
      route_(*vts().getParameter("route")),
      rawRate_(*vts().getParameter("raw_rate")),
      fade_(*vts().getParameter("fade")),
      startPotImm_(*vts().getParameter("start_pot_imm")),
      startCvImm_(*vts().getParameter("start_cv_imm")) {
    init();
    for (int d = 0; d < DECKS; d++) decks_.push_back(std::make_unique<Deck>(vts(), d));
    chooseRoot(DEFAULT_ROOT);
}

static void setParam(RangedAudioParameter& p, float v) {
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(v));
    p.endChangeGesture();
}

void PluginProcessor::chooseRoot(const String& dir) {
    setRoot(dir);
    auto st = radio::readSettings(dir.toStdString());
    if (st.fadeMs >= 0) setParam(fade_, float(st.fadeMs));
    if (st.startPotImmediate >= 0) setParam(startPotImm_, float(st.startPotImmediate));
    if (st.startCvImmediate >= 0) setParam(startCvImm_, float(st.startCvImmediate));
}

PluginProcessor::Deck::Deck(AudioProcessorValueTreeState& apvts, int d)
    : station(*apvts.getParameter(deckId(d, "station"))),
      start(*apvts.getParameter(deckId(d, "start"))),
      speed(*apvts.getParameter(deckId(d, "speed"))),
      noise(*apvts.getParameter(deckId(d, "static"))),
      level(*apvts.getParameter(deckId(d, "level"))),
      bank(*apvts.getParameter(deckId(d, "bank"))),
      reset(*apvts.getParameter(deckId(d, "reset"))) {
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    for (int d = 0; d < DECKS; d++) {
        String n = String(deckName[d]) + " ";
        auto flt = [&](const char* id, const String& name, float lo, float hi, float def) {
            params.add(std::make_unique<ssp::BaseFloatParameter>(deckId(d, id), n + name, lo, hi, def));
        };
        flt("station", "Station", 0.0f, 1.0f, 0.0f);
        flt("start", "Start", 0.0f, 1.0f, 0.0f);
        flt("speed", "Speed", -2.0f, 2.0f, 0.0f);
        flt("static", "Static", 0.0f, 1.0f, 0.0f);
        flt("level", "Level", 0.0f, 1.0f, 0.8f);
        flt("bank", "Bank", 0.0f, 1.0f, 0.0f);
        params.add(std::make_unique<ssp::BaseBoolParameter>(deckId(d, "reset"), n + "Reset", false));
    }
    params.add(std::make_unique<ssp::BaseFloatParameter>("xfade", "A/B", 0.0f, 1.0f, 0.5f));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("route", "Route", StringArray{ "stereo", "split", "random" }, 0));
    StringArray rates;
    for (int r : RAW_RATES) rates.add(String(r));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("raw_rate", "Raw Rate", rates, 2));  // RadioMusic: 44.1k
    // defaults as Radio Music's SETTINGS.TXT; a chosen root's file overrides them
    params.add(std::make_unique<ssp::BaseFloatParameter>("fade", "Fade ms", 1.0f, 500.0f, 15.0f));
    params.add(std::make_unique<ssp::BaseBoolParameter>("start_pot_imm", "Start Pot Imm", false));
    params.add(std::make_unique<ssp::BaseBoolParameter>("start_cv_imm", "Start CV Imm", false));
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static const char* cv[] = { "Station", "Start", "Speed", "Reset" };
    BusesProperties props;
    for (int d = 0; d < DECKS; d++)
        for (int i = 0; i < Engine::I_PER_DECK; i++)
            props.addBus(true, String(deckName[d]) + " " + cv[i], AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R", "A Out", "B Out" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

static float value(RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

void PluginProcessor::control(const float* const*, int) {
    auto& e = radio();
    for (int d = 0; d < DECKS; d++) {
        auto& p = deck(d);
        auto& c = e.controls(d);
        c.station = value(p.station);
        c.start = value(p.start);
        c.speed = value(p.speed);
        c.noise = value(p.noise);
        c.level = value(p.level);
        c.bank = value(p.bank);
        c.reset = p.reset.getValue() > 0.5f;
    }
    e.setCrossfade(value(crossfade_));
    e.setRoute(radio::Route(int(value(route_))));
    e.setRawRate(float(RAW_RATES[std::clamp(int(value(rawRate_)), 0, int(std::size(RAW_RATES)) - 1)]));
    e.setFade(value(fade_));
    e.setStartImmediate(startPotImm_.getValue() > 0.5f, startCvImm_.getValue() > 0.5f);
}

void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("root", root());
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    setRoot(xml->getStringAttribute("root", DEFAULT_ROOT));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, radioPages(*this), radioButtons(*this)),
                                   true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
