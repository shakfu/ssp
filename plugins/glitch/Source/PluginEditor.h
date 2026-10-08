// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#pragma once

#include "PluginProcessor.h"
#include "engine/EngineEditor.h"

StringArray glitchAlgoNames();
std::vector<ssp::engine::ParamPage> glitchPages(PluginProcessor& p);
std::vector<RangedAudioParameter*> glitchButtons(PluginProcessor& p);

// The status panel names each deck's algorithm and what P1 and P2 do in it.
class PluginEditor : public ssp::engine::EngineEditor {
public:
    explicit PluginEditor(PluginProcessor& p);

protected:
    void drawStatus(Graphics& g, Rectangle<int> area) override;

private:
    PluginProcessor& processor_;
};
