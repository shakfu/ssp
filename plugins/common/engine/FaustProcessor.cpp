#include "FaustProcessor.h"

#include "ssp/EditorHost.h"
#include "ssp/controls/BaseParameter.h"

namespace ssp::faust {

// the raw pointer lets every base-class argument read the spec, whatever their evaluation order
FaustProcessor::FaustProcessor(FaustEngineBase* e, juce::Colour colour)
    : EngineProcessor(buses(e->spec()), layout(e->spec()), std::unique_ptr<engine::Engine>(e)), colour_(colour) {
    init();
    for (auto& id : ids(faust().spec())) params_.push_back(vts().getParameter(id));
}

// labels as parameter ids: lower case, [a-z0-9_], a repeated label numbered from 2
juce::StringArray FaustProcessor::ids(const Spec& spec) {
    juce::StringArray out;
    for (auto& k : spec.controls) {
        juce::String id;
        for (auto c : juce::String(k.label).toLowerCase())
            id += juce::String::charToString(juce::CharacterFunctions::isLetterOrDigit(c) ? c : juce::juce_wchar('_'));
        juce::String unique = id;
        for (int i = 2; out.contains(unique); i++) unique = id + "_" + juce::String(i);
        out.add(unique);
    }
    return out;
}

static juce::String channelName(const char* dir, int c, int n) {
    if (n == 1) return dir;
    if (n == 2) return juce::String(dir) + (c == 0 ? " L" : " R");
    return juce::String(dir) + " " + juce::String(c + 1);
}

FaustProcessor::BusesProperties FaustProcessor::buses(const Spec& spec) {
    BusesProperties props;
    for (int c = 0; c < spec.ins; c++) props.addBus(true, channelName("In", c, spec.ins), juce::AudioChannelSet::mono());
    for (auto& k : spec.controls) props.addBus(true, k.label, juce::AudioChannelSet::mono());
    for (int c = 0; c < spec.outs; c++)
        props.addBus(false, channelName("Out", c, spec.outs), juce::AudioChannelSet::mono());
    return props;
}

juce::AudioProcessorValueTreeState::ParameterLayout FaustProcessor::layout(const Spec& spec) {
    juce::AudioProcessorValueTreeState::ParameterLayout params;
    auto id = ids(spec);
    for (size_t i = 0; i < spec.controls.size(); i++) {
        auto& k = spec.controls[i];
        if (k.step > 0.0f)
            params.add(std::make_unique<ssp::BaseFloatParameter>(id[int(i)], k.label, k.lo, k.hi, k.init, k.step));
        else
            params.add(std::make_unique<ssp::BaseFloatParameter>(id[int(i)], k.label, k.lo, k.hi, k.init));
    }
    return params;
}

void FaustProcessor::control(const float* const*, int) {
    for (size_t i = 0; i < params_.size(); i++) faust().set(int(i), params_[i]->convertFrom0to1(params_[i]->getValue()));
}

// four controls a page, in the kernel's order; a step is a twentieth of the range, fine a tenth of that
std::vector<engine::ParamPage> FaustProcessor::pages() {
    std::vector<engine::ParamPage> out;
    auto& controls = faust().spec().controls;
    for (size_t i = 0; i < params_.size(); i++) {
        if (i % 4 == 0) out.push_back({ JucePlugin_Name, colour_ });
        float range = controls[i].hi - controls[i].lo;
        out.back().c[i % 4] = { params_[i], range / 20.0f, range / 200.0f };
    }
    return out;
}

juce::AudioProcessorEditor* FaustProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new engine::EngineEditor(*this, pages(), {}, {}), false);
}

}  // namespace ssp::faust
