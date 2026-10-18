#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

// Load picks a soundfont: an .sf2 file.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;
    void loaded(const String& file, const String& dir) override;
    String browseFrom() override;

private:
    PluginProcessor& processor_;
};
