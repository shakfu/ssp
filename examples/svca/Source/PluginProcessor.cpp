#include "PluginProcessor.h"

#include "ssp/EditorHost.h"

PluginProcessor::PluginProcessor()
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::make_unique<svca::SvcaEngine>()),
      level_(*vts().getParameter("level")) {
    init();
}

AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout() {
    AudioProcessorValueTreeState::ParameterLayout params;
    params.add(std::make_unique<ssp::BaseFloatParameter>("level", "Level", 0.0f, 2.0f, 1.0f));
    return params;
}

// one mono bus per jack, in the engine's I_ and O_ order
PluginProcessor::BusesProperties PluginProcessor::getBusesProperties() {
    BusesProperties props;
    for (auto* name : { "In L", "In R", "Level CV" }) props.addBus(true, name, AudioChannelSet::mono());
    for (auto* name : { "Out L", "Out R" }) props.addBus(false, name, AudioChannelSet::mono());
    return props;
}

// audio thread, before each Engine::process: parameters to the engine
void PluginProcessor::control(const float* const*, int) {
    vca().level = level_.convertFrom0to1(level_.getValue());
}

// one page of up to four encoders; coarse and fine steps are in the parameter's units
std::vector<ssp::engine::ParamPage> PluginProcessor::pages() {
    return { { "vca", Colours::orange, { { &level_, 0.05f, 0.005f } } } };
}

AudioProcessorEditor* PluginProcessor::createEditor() {
    if (useCompactUI())
        return new ssp::EditorHost(this, new ssp::engine::EngineMiniEditor(*this, pages(), {}), true);
    return new ssp::EditorHost(this, new ssp::engine::EngineEditor(*this, pages(), {}, {}), false);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PluginProcessor();
}
