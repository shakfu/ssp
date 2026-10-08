// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#include "PluginEditor.h"

static const Colour deckClr[] = { Colour(230, 90, 80), Colour(80, 170, 230) };

// per algorithm, in glitch::Algo order: name, P1, P2, whether Pitch acts
struct AlgoInfo {
    const char *name, *p1, *p2;
    bool pitched;
};
static const AlgoInfo algos[] = {
    { "sparse", "speed", "silence", true },      { "wander", "speed", "walk rate", true },
    { "bitmangle", "speed", "bit clock", true }, { "tri xor", "osc 1", "osc 2", true },
    { "nand", "osc 1", "osc 2", true },          { "fm noise", "freq", "reroll clock", true },
    { "ring mod", "osc 1", "osc 2", true },      { "phrygian", "trigger rate", "burst", true },
    { "penta", "clock", "decay", true },         { "bernoulli", "chance 1", "chance 2", true },
    { "dust", "density", "tone", false },        { "rhythm", "clock", "division", false },
};
static_assert(std::size(algos) == glitch::kAlgoCount);

StringArray glitchAlgoNames() {
    StringArray names;
    for (auto& a : algos) names.add(a.name);
    return names;
}

std::vector<ssp::engine::ParamPage> glitchPages(PluginProcessor& p) {
    std::vector<ssp::engine::ParamPage> pages;
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        size_t first = pages.size();
        auto& k = p.deck(d);
        String n = d == 0 ? "A " : "B ";
        pages.push_back({ n + "voice", deckClr[d],
                          { { &k.algo, 1.0f, 1.0f }, { &k.p1, 0.05f, 0.005f }, { &k.p2, 0.05f, 0.005f },
                            { &k.pitch, 0.05f, 0.005f } } });
        pages.push_back({ n + "out", deckClr[d], { { &k.tone, 0.05f, 0.01f }, { &k.level, 0.05f, 0.01f } } });
        for (size_t i = first; i < pages.size(); i++) pages[i].group = d;
    }
    size_t globals = pages.size();
    pages.push_back({ "mix", Colours::white, { { &p.crossfade(), 0.05f, 0.01f }, { &p.route(), 1.0f, 1.0f } } });
    for (size_t i = globals; i < pages.size(); i++) pages[i].group = PluginProcessor::DECKS;  // the global pages
    return pages;
}

std::vector<RangedAudioParameter*> glitchButtons(PluginProcessor& p) {
    return { &p.deck(0).regen, &p.deck(1).regen };
}

PluginEditor::PluginEditor(PluginProcessor& p) : EngineEditor(p, glitchPages(p), glitchButtons(p), {}), processor_(p) {
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        auto& a = algos[std::clamp(processor_.glitch().algo(d), 0, glitch::kAlgoCount - 1)];
        g.setColour(deckClr[d]);
        g.drawText(String(d == 0 ? "A " : "B ") + a.name, area.removeFromTop(lineH), Justification::left);
        g.setColour(Colours::grey);
        String help = String("  P1 ") + a.p1 + ", P2 " + a.p2 + (a.pitched ? "" : ", no pitch");
        g.drawText(help, area.removeFromTop(lineH), Justification::left);
        area.removeFromTop(lineH / 2);
    }
}
