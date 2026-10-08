#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

std::vector<ssp::engine::ParamPage> bardPages(PluginProcessor& p);
std::vector<RangedAudioParameter*> bardButtons(PluginProcessor& p);

// Load sets the library root: the directory being browsed, or the selected file's folder.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;
    void loaded(const String& file, const String& dir) override;
    String browseFrom() override { return processor_.root(); }

private:
    PluginProcessor& processor_;
};
