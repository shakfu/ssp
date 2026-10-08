#include "PluginProcessor.h"
#include "SSPApi.h"

static const Colour colour(200, 170, 110);

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginDescriptor* createDescriptor() {
    auto desc = new Percussa::SSP::PluginDescriptor;
    SSP_defaultDescriptor(desc);
    desc->colour = colour.getARGB();
    return desc;
}

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginInterface* createInstance() {
#ifdef JUCE_DEBUG
    ScopedJuceInitialiser_GUI juceInitialiser_;
#endif
    return new SSP_PluginInterface(new PluginProcessor());
}

extern "C" __attribute__((visibility("default"))) bool apiExtensions() {
    return true;
}

extern "C" __attribute__((visibility("default"))) Percussa::SSP::PluginDescriptor* createExtendedDescriptor() {
    auto desc = new SSPExtendedApi::PluginDescriptor;
    SSP_defaultDescriptor(desc);
    desc->colour = colour.getARGB();
    desc->supportCompactUI_ = true;
    desc->categories_.push_back(CAT_OSCILLATOR);
    return desc;
}
