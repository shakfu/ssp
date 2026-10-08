#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "ssp/EditorHost.h"

static String drumId(int d, const char* id) {
    return String(d + 1) + ":" + id;
}

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<edrums::EdrumsEngine>()),
      route_(*vts().getParameter("route")) {
    init();
    for (int d = 0; d < DRUMS; d++) drums_.push_back(std::make_unique<Drum>(vts(), d));
}

PluginProcessor::Drum::Drum(AudioProcessorValueTreeState& apvts, int d)
    : model(*apvts.getParameter(drumId(d, "model"))),
      hits(*apvts.getParameter(drumId(d, "hits"))),
      steps(*apvts.getParameter(drumId(d, "steps"))),
      rotate(*apvts.getParameter(drumId(d, "rotate"))),
      rate(*apvts.getParameter(drumId(d, "rate"))),
      chance(*apvts.getParameter(drumId(d, "chance"))),
      swing(*apvts.getParameter(drumId(d, "swing"))),
      pitch(*apvts.getParameter(drumId(d, "pitch"))),
      decay(*apvts.getParameter(drumId(d, "decay"))),
      level(*apvts.getParameter(drumId(d, "level"))),
      drive(*apvts.getParameter(drumId(d, "drive"))),
      sweep(*apvts.getParameter(drumId(d, "sweep"))),
      tone(*apvts.getParameter(drumId(d, "tone"))),
      bright(*apvts.getParameter(drumId(d, "bright"))),
      mute(*apvts.getParameter(drumId(d, "mute"))),
      voice(*apvts.getParameter(drumId(d, "voice"))),
      trig(*apvts.getParameter(drumId(d, "trig"))) {
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    // the default kit: kick on the beat, tom silent, snare on 2 and 4, hat on the off-beats
    struct Kit {
        edrums::Model model;
        int hits, rotate;
        float pitch, decay;
    };
    static const Kit kit[DRUMS] = { { edrums::Model::Kick, 4, 0, 0.5f, 0.5f },
                                    { edrums::Model::Tom, 0, 0, 0.4f, 0.55f },
                                    { edrums::Model::Snare, 2, 4, 0.5f, 0.5f },
                                    { edrums::Model::Hat, 4, 2, 0.82f, 0.3f } };
    AudioProcessorValueTreeState::ParameterLayout params;
    for (int d = 0; d < DRUMS; d++) {
        String n = String(d + 1) + " ";
        auto flt = [&](const char* id, const String& name, float def) {
            params.add(std::make_unique<ssp::BaseFloatParameter>(drumId(d, id), n + name, 0.0f, 1.0f, def));
        };
        auto num = [&](const char* id, const String& name, int lo, int hi, int def) {
            params.add(std::make_unique<ssp::BaseFloatParameter>(drumId(d, id), n + name, float(lo), float(hi),
                                                                 float(def), 1.0f));
        };
        params.add(std::make_unique<ssp::BaseChoiceParameter>(drumId(d, "model"), n + "Model", edrumsModelNames(),
                                                              int(kit[d].model)));
        num("hits", "Hits", 0, 16, kit[d].hits);
        num("steps", "Steps", 1, 16, 16);
        num("rotate", "Rotate", 0, 15, kit[d].rotate);
        params.add(std::make_unique<ssp::BaseChoiceParameter>(drumId(d, "rate"), n + "Rate", edrumsRateNames(),
                                                              EDRUMS_RATE_X1));
        flt("chance", "Chance", 1.0f);
        num("swing", "Swing", 50, 75, 50);  // percent
        flt("pitch", "Pitch", kit[d].pitch);
        flt("decay", "Decay", kit[d].decay);
        flt("level", "Level", 0.8f);
        flt("drive", "Drive", 0.5f);
        flt("sweep", "Sweep", 0.5f);
        flt("tone", "Tone", 0.5f);
        flt("bright", "Bright", 0.5f);
        params.add(std::make_unique<ssp::BaseBoolParameter>(drumId(d, "mute"), n + "Mute", false));
        params.add(std::make_unique<ssp::BaseBoolParameter>(drumId(d, "voice"), n + "Voice", true));
        params.add(std::make_unique<ssp::BaseBoolParameter>(drumId(d, "trig"), n + "Trig", false));
    }
    params.add(std::make_unique<ssp::BaseChoiceParameter>("route", "Route", StringArray{ "stereo", "split", "random" }, 0));
    return params;
}

PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "Clock", "Reset" }) props.addBus(true, name, AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R", "1 Out", "2 Out", "3 Out", "4 Out", "1 Trig", "2 Trig", "3 Trig", "4 Trig" })
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
    auto& e = edrums();
    for (int d = 0; d < DRUMS; d++) {
        auto& p = drum(d);
        auto& c = e.controls(d);
        c.model = edrums::Model(whole(p.model));
        c.hits = whole(p.hits);
        c.steps = whole(p.steps);
        c.rotate = whole(p.rotate);
        // rate index: /8 .. /2, x1, x2 .. x8
        int r = whole(p.rate) - EDRUMS_RATE_X1;
        c.mult = r > 0 ? r + 1 : 1;
        c.div = r < 0 ? 1 - r : 1;
        c.chance = value(p.chance);
        c.swing = value(p.swing) / 100.0f;
        c.pitch = value(p.pitch);
        c.decay = value(p.decay);
        c.level = value(p.level);
        c.drive = value(p.drive);
        c.sweep = value(p.sweep);
        c.tone = value(p.tone);
        c.bright = value(p.bright);
        c.mute = p.mute.getValue() > 0.5f;
        c.voice = p.voice.getValue() > 0.5f;
        c.trigger = p.trig.getValue() > 0.5f;
    }
    e.setRoute(edrums::Route(whole(route_)));
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, edrumsPages(*this), edrumsButtons(*this)),
                                   true);
    return new ssp::EditorHost(this, new PluginEditor(*this), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
