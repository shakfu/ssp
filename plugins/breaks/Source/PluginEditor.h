#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

// Load picks a card folder, as _core's sample manager writes it: the folder, a bank folder in it, a
// file in one, or a folder holding cards.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;
    void loaded(const String& file, const String& dir) override;
    String browseFrom() override;
    void step(int dir) override;  // Left and Right: the previous and next bank

private:
    PluginProcessor& processor_;
};
