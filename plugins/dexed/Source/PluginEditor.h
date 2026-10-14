#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

// Load picks a cartridge: a .syx bulk dump of 32 voices. PG- and PG+ step through its voices.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;
    void loaded(const String& file, const String& dir) override;
    String browseFrom() override;
    void step(int dir) override;

private:
    PluginProcessor& processor_;
};
