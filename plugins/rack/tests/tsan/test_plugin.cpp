// Stateless pass-through module with CHANNELS inputs and outputs, loaded by track_race_test.

#include "SSPExApi.h"

namespace {

struct Descriptor : SSPExtendedApi::PluginDescriptor {
    Descriptor() {
        name = "pass" + std::to_string(CHANNELS);
        for (int c = 0; c < CHANNELS; c++) {
            inputChannelNames.push_back("In " + std::to_string(c));
            outputChannelNames.push_back("Out " + std::to_string(c));
        }
        supportCompactUI_ = true;
    }
};

struct Plugin : SSPExtendedApi::PluginInterface {
    SSPExtendedApi::PluginEditorInterface* getEditor() override { return nullptr; }
    void getState(void** buffer, size_t* size) override { *buffer = nullptr, *size = 0; }
    void prepare(double, int) override {}
    void process(float** channelData, int numChannels, int numSamples) override {
        for (int c = 0; c < numChannels; c++) {
            for (int i = 0; i < numSamples; i++) channelData[c][i] *= 0.5f;
        }
    }
    void useCompactUI(bool) override {}
    unsigned numberOfParameters() override { return 0; }
    bool parameterDesc(unsigned, ParameterDesc&) override { return false; }
    float parameterValue(unsigned) override { return 0.f; }
    bool parameterValue(unsigned, float) override { return false; }
};

}  // namespace

extern "C" {
__attribute__((visibility("default"))) bool apiExtensions() { return true; }
__attribute__((visibility("default"))) Percussa::SSP::PluginInterface* createInstance() { return new Plugin(); }
__attribute__((visibility("default"))) SSPExtendedApi::PluginDescriptor* createExtendedDescriptor() {
    return new Descriptor();
}
}
