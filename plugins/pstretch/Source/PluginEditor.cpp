#include "PluginEditor.h"

static const Colour deckClr[] = { Colour(230, 90, 80), Colour(80, 170, 230) };

std::vector<ssp::engine::ParamPage> pstretchPages(PluginProcessor& p) {
    std::vector<ssp::engine::ParamPage> pages;
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        size_t first = pages.size();
        auto& k = p.deck(d);
        String n = d == 0 ? "A " : "B ";
        pages.push_back({ n + "stretch", deckClr[d],
                          { { &k.stretch, 0.05f, 0.005f }, { &k.diffuse, 0.05f, 0.01f }, { &k.pitch, 0.05f, 0.005f },
                            { &k.tone, 0.05f, 0.01f } } });
        pages.push_back({ n + "source", deckClr[d],
                          { { &k.source, 1.0f, 1.0f }, { &k.clip, 0.05f, 0.005f }, { &k.position, 0.05f, 0.005f },
                            { &k.mix, 0.05f, 0.01f } } });
        pages.push_back({ n + "mod", deckClr[d],
                          { { &k.modRate, 0.05f, 0.005f }, { &k.modDepth, 0.05f, 0.01f }, { &k.modShape, 1.0f, 1.0f },
                            { &k.modTarget, 1.0f, 1.0f } } });
        for (size_t i = first; i < pages.size(); i++) pages[i].group = d;
    }
    size_t globals = pages.size();
    pages.push_back({ "mix", Colours::white,
                      { { &p.crossfade(), 0.05f, 0.01f }, { &p.route(), 1.0f, 1.0f }, { &p.window(), 1.0f, 1.0f } } });
    for (size_t i = globals; i < pages.size(); i++) pages[i].group = PluginProcessor::DECKS;  // the global pages
    return pages;
}

std::vector<RangedAudioParameter*> pstretchButtons(PluginProcessor& p) {
    return { &p.deck(0).freeze, &p.deck(0).grab, &p.deck(1).freeze, &p.deck(1).grab };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, pstretchPages(p), pstretchButtons(p), p.root()), processor_(p) {
}

void PluginEditor::loaded(const String& file, const String& dir) {
    processor_.setRoot(file.isEmpty() ? dir : File(file).getParentDirectory().getFullPathName());
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    static const char* sources[] = { "live", "capture", "file" };
    g.setColour(Colours::grey);
    g.drawText("clips " + processor_.root(), area.removeFromTop(lineH), Justification::left);
    area.removeFromTop(lineH / 2);
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        auto i = processor_.pstretch().info(d);
        auto& k = processor_.deck(d);
        int source = std::clamp(int(std::lround(k.source.convertFrom0to1(k.source.getValue()))), 0, 2);
        float factor = std::pow(64.0f, k.stretch.convertFrom0to1(k.stretch.getValue()));
        g.setColour(deckClr[d]);
        g.drawText(String(d == 0 ? "A " : "B ") + sources[source] + String::formatted("  %.1fx", factor) +
                       (i.frozen ? "  frozen" : ""),
                   area.removeFromTop(lineH), Justification::left);
        if (source == int(pstretch::Source::File)) {
            String clip = i.clips == 0 ? String("no clips")
                          : i.clip < 0  ? String("-")
                                        : String(i.clip + 1) + "/" + String(i.clips) + " " + i.clipName;
            g.setColour(Colours::white);
            g.drawText("  " + clip, area.removeFromTop(lineH), Justification::left);
        }
        area.removeFromTop(lineH / 2);
    }
    g.setColour(Colours::grey);
    g.drawText("window " + String(processor_.pstretch().info(0).window), area.removeFromTop(lineH),
               Justification::left);
}
