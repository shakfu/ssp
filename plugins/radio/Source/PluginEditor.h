#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

std::vector<ssp::engine::ParamPage> radioPages(PluginProcessor& p);
std::vector<RangedAudioParameter*> radioButtons(PluginProcessor& p);

// Load sets the library root: the directory being browsed when Load is pressed.
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
