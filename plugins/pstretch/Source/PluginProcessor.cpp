#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static const char* DEFAULT_ROOT = "/media/BOOT/pstretch";
static const char* deckName[] = { "A", "B" };

static String deckId(int d, const char* id) {
    return String(deckName[d]).toLowerCase() + ":" + id;
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<pstretch::PstretchEngine>()),
      crossfade_(*vts().getParameter("xfade")),
      route_(*vts().getParameter("route")),
      window_(*vts().getParameter("window")) {
    init();
    for (int d = 0; d < DECKS; d++) decks_.push_back(std::make_unique<Deck>(vts(), d));
    setRoot(DEFAULT_ROOT);
}

PluginProcessor::Deck::Deck(AudioProcessorValueTreeState& apvts, int d)
    : stretch(*apvts.getParameter(deckId(d, "stretch"))),
      diffuse(*apvts.getParameter(deckId(d, "diffuse"))),
      pitch(*apvts.getParameter(deckId(d, "pitch"))),
      tone(*apvts.getParameter(deckId(d, "tone"))),
      mix(*apvts.getParameter(deckId(d, "mix"))),
      source(*apvts.getParameter(deckId(d, "source"))),
      clip(*apvts.getParameter(deckId(d, "clip"))),
      position(*apvts.getParameter(deckId(d, "position"))),
      modRate(*apvts.getParameter(deckId(d, "mod_rate"))),
      modDepth(*apvts.getParameter(deckId(d, "mod_depth"))),
      modShape(*apvts.getParameter(deckId(d, "mod_shape"))),
      modTarget(*apvts.getParameter(deckId(d, "mod_target"))),
      freeze(*apvts.getParameter(deckId(d, "freeze"))),
      grab(*apvts.getParameter(deckId(d, "grab"))) {
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    for (int d = 0; d < DECKS; d++) {
        String n = String(deckName[d]) + " ";
        auto flt = [&](const char* id, const String& name, float def) {
            params.add(std::make_unique<ssp::BaseFloatParameter>(deckId(d, id), n + name, 0.0f, 1.0f, def));
        };
        auto choice = [&](const char* id, const String& name, const StringArray& items) {
            params.add(std::make_unique<ssp::BaseChoiceParameter>(deckId(d, id), n + name, items, 0));
        };
        flt("stretch", "Stretch", 0.5f);
        flt("diffuse", "Diffuse", 1.0f);
        flt("pitch", "Pitch", 0.5f);
        flt("tone", "Tone", 1.0f);
        flt("mix", "Mix", 1.0f);
        choice("source", "Source", { "live", "capture", "file" });
        flt("clip", "Clip", 0.0f);
        flt("position", "Position", 0.0f);
        flt("mod_rate", "Mod Rate", 0.3f);
        flt("mod_depth", "Mod Depth", 0.0f);
        choice("mod_shape", "Mod Shape", { "sine", "triangle", "follow" });
        choice("mod_target", "Mod Target", { "diffuse", "stretch", "tone" });
        params.add(std::make_unique<ssp::BaseBoolParameter>(deckId(d, "freeze"), n + "Freeze", false));
        params.add(std::make_unique<ssp::BaseBoolParameter>(deckId(d, "grab"), n + "Grab", false));
    }
    params.add(std::make_unique<ssp::BaseFloatParameter>("xfade", "A/B", 0.0f, 1.0f, 0.5f));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("route", "Route", StringArray{ "stereo", "split", "random" }, 0));
    StringArray windows;
    for (int w : Engine::WINDOWS) windows.add(String(w));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("window", "Window", windows, 1));  // 8192, as sk-engines
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static const char* in[] = { "In", "Pitch", "Stretch", "Mix", "Gate" };
    BusesProperties props;
    for (int d = 0; d < DECKS; d++)
        for (int i = 0; i < Engine::I_PER_DECK; i++)
            props.addBus(true, String(deckName[d]) + " " + in[i], AudioChannelSet::mono());
    props.addBus(true, "A/B", AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R", "A Out", "B Out", "A LFO", "B LFO", "A Gate", "B Gate" })
        props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

static float value(RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

static int whole(RangedAudioParameter& p) {
    return int(std::lround(value(p)));
}

void PluginProcessor::control(const float* const*, int) {
    auto& e = pstretch();
    for (int d = 0; d < DECKS; d++) {
        auto& p = deck(d);
        auto& c = e.controls(d);
        c.stretch = value(p.stretch);
        c.diffuse = value(p.diffuse);
        c.pitch = value(p.pitch);
        c.tone = value(p.tone);
        c.mix = value(p.mix);
        c.source = pstretch::Source(whole(p.source));
        c.clip = value(p.clip);
        c.position = value(p.position);
        c.modRate = value(p.modRate);
        c.modDepth = value(p.modDepth);
        c.modShape = pstretch::ModShape(whole(p.modShape));
        c.modTarget = pstretch::ModTarget(whole(p.modTarget));
        c.freeze = p.freeze.getValue() > 0.5f;
        c.grab = p.grab.getValue() > 0.5f;
    }
    e.setCrossfade(value(crossfade_));
    e.setRoute(pstretch::Route(whole(route_)));
    e.setWindow(Engine::WINDOWS[std::clamp(whole(window_), 0, int(std::size(Engine::WINDOWS)) - 1)]);
}

void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("root", root());
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    setRoot(xml->getStringAttribute("root", DEFAULT_ROOT));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(
            this, new ssp::engine::EngineMiniEditor(*this, pstretchPages(*this), pstretchButtons(*this)), true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
