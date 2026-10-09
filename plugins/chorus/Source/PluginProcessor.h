#pragma once

#include "ChorusKernel.h"
#include "engine/FaustProcessor.h"

using namespace juce;

class PluginProcessor : public ssp::faust::FaustProcessor {
public:
    using Engine = ssp::faust::FaustEngine<ssp::faust::chorus::mydsp>;
    static juce::Colour colour() { return { 51, 204, 255 }; }

    PluginProcessor() : FaustProcessor(new Engine, colour()) {}

    // SSPApi.h names the channels before an instance exists
    static BusesProperties getBusesProperties() { return buses(Engine().spec()); }
};
