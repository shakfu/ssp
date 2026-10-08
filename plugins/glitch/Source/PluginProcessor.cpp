// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static const char* deckName[] = { "A", "B" };

static String deckId(int d, const char* id) {
    return String(deckName[d]).toLowerCase() + ":" + id;
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<glitch::GlitchEngine>()),
      crossfade_(*vts().getParameter("xfade")),
      route_(*vts().getParameter("route")) {
    init();
    for (int d = 0; d < DECKS; d++) decks_.push_back(std::make_unique<Deck>(vts(), d));
}

PluginProcessor::Deck::Deck(AudioProcessorValueTreeState& apvts, int d)
    : algo(*apvts.getParameter(deckId(d, "algo"))),
      p1(*apvts.getParameter(deckId(d, "p1"))),
      p2(*apvts.getParameter(deckId(d, "p2"))),
      pitch(*apvts.getParameter(deckId(d, "pitch"))),
      tone(*apvts.getParameter(deckId(d, "tone"))),
      level(*apvts.getParameter(deckId(d, "level"))),
      regen(*apvts.getParameter(deckId(d, "regen"))) {
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    for (int d = 0; d < DECKS; d++) {
        String n = String(deckName[d]) + " ";
        auto flt = [&](const char* id, const String& name, float def) {
            params.add(std::make_unique<ssp::BaseFloatParameter>(deckId(d, id), n + name, 0.0f, 1.0f, def));
        };
        params.add(std::make_unique<ssp::BaseChoiceParameter>(deckId(d, "algo"), n + "Algo", glitchAlgoNames(), 0));
        flt("p1", "P1", 0.5f);
        flt("p2", "P2", 0.5f);
        flt("pitch", "Pitch", 0.5f);
        flt("tone", "Tone", 1.0f);
        flt("level", "Level", 0.8f);
        params.add(std::make_unique<ssp::BaseBoolParameter>(deckId(d, "regen"), n + "Regen", false));
    }
    params.add(std::make_unique<ssp::BaseFloatParameter>("xfade", "A/B", 0.0f, 1.0f, 0.5f));
    params.add(std::make_unique<ssp::BaseChoiceParameter>("route", "Route", StringArray{ "stereo", "split", "random" }, 0));
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    static const char* cv[] = { "Pitch", "P1", "P2" };
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
    auto& e = glitch();
    for (int d = 0; d < DECKS; d++) {
        auto& p = deck(d);
        auto& c = e.controls(d);
        c.algo = int(value(p.algo));
        c.p1 = value(p.p1);
        c.p2 = value(p.p2);
        c.pitch = value(p.pitch);
        c.tone = value(p.tone);
        c.level = value(p.level);
        c.regen = p.regen.getValue() > 0.5f;
    }
    e.setCrossfade(value(crossfade_));
    e.setRoute(glitch::Route(int(value(route_))));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, glitchPages(*this), glitchButtons(*this)),
                                   true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
