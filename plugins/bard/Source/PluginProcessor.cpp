#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static const char* DEFAULT_ROOT = "/media/BOOT/bard";
static const char* deckName[] = { "A", "B" };

static String deckId(int d, const char* id) {
    return String(deckName[d]).toLowerCase() + ":" + id;
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<bard::BardEngine>()),
      crossfade_(*vts().getParameter("xfade")),
      route_(*vts().getParameter("route")) {
    init();
    for (int d = 0; d < DECKS; d++) decks_.push_back(std::make_unique<Deck>(vts(), d));
    setRoot(DEFAULT_ROOT);
}

PluginProcessor::Deck::Deck(AudioProcessorValueTreeState& apvts, int d)
    : book(*apvts.getParameter(deckId(d, "book"))),
      mark(*apvts.getParameter(deckId(d, "mark"))),
      rate(*apvts.getParameter(deckId(d, "rate"))),
      keep(*apvts.getParameter(deckId(d, "keep"))),
      volume(*apvts.getParameter(deckId(d, "volume"))),
      shelf(*apvts.getParameter(deckId(d, "shelf"))),
      position(*apvts.getParameter(deckId(d, "position"))),
      seq(*apvts.getParameter(deckId(d, "seq"))),
      loop(*apvts.getParameter(deckId(d, "loop"))),
      reroll(*apvts.getParameter(deckId(d, "reroll"))),
      seam(*apvts.getParameter(deckId(d, "seam"))),
      duck(*apvts.getParameter(deckId(d, "duck"))),
      release(*apvts.getParameter(deckId(d, "release"))),
      colour(*apvts.getParameter(deckId(d, "colour"))),
      colourMix(*apvts.getParameter(deckId(d, "colour_mix"))),
      room(*apvts.getParameter(deckId(d, "room"))),
      roomMix(*apvts.getParameter(deckId(d, "room_mix"))),
      character(*apvts.getParameter(deckId(d, "character"))),
      play(*apvts.getParameter(deckId(d, "play"))),
      back(*apvts.getParameter(deckId(d, "back"))),
      next(*apvts.getParameter(deckId(d, "next"))) {
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
        auto button = [&](const char* id, const String& name) {
            params.add(std::make_unique<ssp::BaseBoolParameter>(deckId(d, id), n + name, false));
        };
        flt("book", "Book", 0.0f);
        flt("mark", "Mark", 0.0f);
        flt("rate", "Rate", 0.5f);
        flt("keep", "Keep", 0.0f);
        flt("volume", "Volume", 1.0f);
        flt("shelf", "Shelf", 0.0f);
        flt("position", "Position", 0.0f);
        choice("seq", "Seq", { "read", "recite", "wander" });
        choice("loop", "Loop", { "file", "hold", "loop" });
        params.add(std::make_unique<ssp::BaseFloatParameter>(deckId(d, "reroll"), n + "Reroll", 0.0f, 99.0f, 0.0f, 1.0f));
        flt("seam", "Seam", 0.0f);
        flt("duck", "Duck", 0.0f);
        flt("release", "Release", 0.5f);
        flt("colour", "Colour", 0.0f);
        flt("colour_mix", "Colour Mix", 0.0f);
        flt("room", "Room", 0.5f);
        flt("room_mix", "Room Mix", 0.0f);
        choice("character", "Character", { "plate", "hall", "slap" });
        button("play", "Play");
        button("back", "Back");
        button("next", "Next");
    }
    params.add(std::make_unique<ssp::BaseFloatParameter>("xfade", "A/B", 0.0f, 1.0f, 0.5f));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("route", "Route", StringArray{ "stereo", "split", "random" }, 0));
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static const char* in[] = { "Book", "Mark", "Volume", "Gate" };
    BusesProperties props;
    for (int d = 0; d < DECKS; d++)
        for (int i = 0; i < Engine::I_PER_DECK; i++)
            props.addBus(true, String(deckName[d]) + " " + in[i], AudioChannelSet::mono());
    props.addBus(true, "A/B", AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R", "A Out", "B Out", "A Env", "B Env", "A Gate", "B Gate" })
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
    auto& e = bard();
    for (int d = 0; d < DECKS; d++) {
        auto& p = deck(d);
        auto& c = e.controls(d);
        c.book = value(p.book);
        c.mark = value(p.mark);
        c.rate = value(p.rate);
        c.keep = value(p.keep);
        c.volume = value(p.volume);
        c.shelf = value(p.shelf);
        c.position = value(p.position);
        c.seq = bard::Seq(whole(p.seq));
        c.loop = bard::LoopPolicy(whole(p.loop));
        c.reroll = whole(p.reroll);
        c.seam = value(p.seam);
        c.duck = value(p.duck);
        c.release = value(p.release);
        c.colour = value(p.colour);
        c.colourMix = value(p.colourMix);
        c.room = value(p.room);
        c.roomMix = value(p.roomMix);
        c.character = bard::Room::Character(whole(p.character));
        c.play = p.play.getValue() > 0.5f;
        c.back = p.back.getValue() > 0.5f;
        c.next = p.next.getValue() > 0.5f;
    }
    e.setCrossfade(value(crossfade_));
    e.setRoute(bard::Route(whole(route_)));
}

void PluginProcessor::customToXml(XmlElement* xml) {
    xml->setAttribute("root", root());
}

void PluginProcessor::customFromXml(XmlElement* xml) {
    setRoot(xml->getStringAttribute("root", DEFAULT_ROOT));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, bardPages(*this), bardButtons(*this)),
                                   true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
