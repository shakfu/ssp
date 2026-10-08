#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

StringArray edrumsModelNames();
StringArray edrumsRateNames();  // /8 .. /2, x1, x2 .. x8
static constexpr int EDRUMS_RATE_X1 = 7;
std::vector<ssp::engine::ParamPage> edrumsPages(PluginProcessor& p);
std::vector<RangedAudioParameter*> edrumsButtons(PluginProcessor& p);

// The status panel shows each track's steps: onsets lit, the next step outlined, a flash on each hit.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;

private:
    PluginProcessor& processor_;
    uint32_t lastHits_[PluginProcessor::DRUMS] = {};
    int flash_[PluginProcessor::DRUMS] = {};
};
