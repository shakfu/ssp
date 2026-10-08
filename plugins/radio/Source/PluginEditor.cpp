#include "PluginEditor.h"

static const Colour deckClr[] = { Colour(230, 90, 80), Colour(80, 170, 230) };

std::vector<ssp::engine::ParamPage> radioPages(PluginProcessor& p) {
    std::vector<ssp::engine::ParamPage> pages;
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        size_t first = pages.size();
        auto& k = p.deck(d);
        String n = d == 0 ? "A " : "B ";
        pages.push_back({ n + "tune", deckClr[d],
                          { { &k.station, 0.05f, 0.005f }, { &k.start, 0.05f, 0.005f }, { &k.speed, 0.1f, 0.01f },
                            { &k.level, 0.05f, 0.01f } } });
        pages.push_back({ n + "bank", deckClr[d], { { &k.bank, 0.05f, 0.005f }, { &k.noise, 0.05f, 0.01f } } });
        for (size_t i = first; i < pages.size(); i++) pages[i].group = d;
    }
    size_t globals = pages.size();
    pages.push_back({ "mix", Colours::white,
                      { { &p.crossfade(), 0.05f, 0.01f }, { &p.route(), 1.0f, 1.0f }, { &p.rawRate(), 1.0f, 1.0f } } });
    pages.push_back({ "settings", Colours::white,
                      { { &p.fade(), 5.0f, 1.0f }, { &p.startPotImmediate(), 1.0f, 1.0f },
                        { &p.startCvImmediate(), 1.0f, 1.0f } } });
    for (size_t i = globals; i < pages.size(); i++) pages[i].group = PluginProcessor::DECKS;  // the global pages
    return pages;
}

std::vector<RangedAudioParameter*> radioButtons(PluginProcessor& p) {
    return { &p.deck(0).reset, &p.deck(1).reset };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, radioPages(p), radioButtons(p), p.root()), processor_(p) {
}

void PluginEditor::loaded(const String& file, const String& dir) {
    processor_.chooseRoot(file.isEmpty() ? dir : File(file).getParentDirectory().getFullPathName());
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    g.setColour(Colours::grey);
    g.drawText("root " + processor_.root(), area.removeFromTop(lineH), Justification::left);
    area.removeFromTop(lineH / 2);
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        auto i = processor_.radio().info(d);
        g.setColour(deckClr[d]);
        String bank = i.banks == 0 ? String("no banks")
                                   : "bank " + String(i.bank + 1) + "/" + String(i.banks) + " " + i.bankName;
        g.drawText(String(d == 0 ? "A " : "B ") + bank, area.removeFromTop(lineH), Justification::left);
        String station = i.stations == 0 ? String("no stations")
                         : i.station < 0 ? String("-")
                                         : String(i.station + 1) + "/" + String(i.stations) + " " + i.stationName;
        if (i.error) station += " (read error)";
        g.setColour(i.playing ? Colours::white : Colours::grey);
        g.drawText("  " + station, area.removeFromTop(lineH), Justification::left);
        area.removeFromTop(lineH / 2);
    }
}
