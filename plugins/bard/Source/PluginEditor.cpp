#include "PluginEditor.h"

static const Colour deckClr[] = { Colour(230, 90, 80), Colour(80, 170, 230) };

std::vector<ssp::engine::ParamPage> bardPages(PluginProcessor& p) {
    std::vector<ssp::engine::ParamPage> pages;
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        size_t first = pages.size();
        auto& k = p.deck(d);
        String n = d == 0 ? "A " : "B ";
        pages.push_back({ n + "book", deckClr[d],
                          { { &k.book, 0.05f, 0.005f }, { &k.mark, 0.05f, 0.005f }, { &k.rate, 0.05f, 0.005f },
                            { &k.keep, 0.05f, 0.01f } } });
        pages.push_back({ n + "shelf", deckClr[d],
                          { { &k.shelf, 0.05f, 0.005f }, { &k.position, 0.05f, 0.002f }, { &k.volume, 0.05f, 0.01f },
                            { &k.seq, 1.0f, 1.0f } } });
        pages.push_back({ n + "voice", deckClr[d],
                          { { &k.colour, 0.05f, 0.01f }, { &k.colourMix, 0.05f, 0.01f }, { &k.room, 0.05f, 0.01f },
                            { &k.roomMix, 0.05f, 0.01f } } });
        pages.push_back({ n + "more", deckClr[d],
                          { { &k.character, 1.0f, 1.0f }, { &k.seam, 0.05f, 0.01f }, { &k.duck, 0.05f, 0.01f },
                            { &k.release, 0.05f, 0.01f } } });
        pages.push_back({ n + "marks", deckClr[d], { { &k.loop, 1.0f, 1.0f }, { &k.reroll, 1.0f, 1.0f } } });
        for (size_t i = first; i < pages.size(); i++) pages[i].group = d;
    }
    size_t globals = pages.size();
    pages.push_back({ "mix", Colours::white, { { &p.crossfade(), 0.05f, 0.01f }, { &p.route(), 1.0f, 1.0f } } });
    for (size_t i = globals; i < pages.size(); i++) pages[i].group = PluginProcessor::DECKS;  // the global pages
    return pages;
}

// buttons 1-4: play and back per deck; 6 and 8: next. 5 is Load.
std::vector<RangedAudioParameter*> bardButtons(PluginProcessor& p) {
    auto &a = p.deck(0), &b = p.deck(1);
    return { &a.play, &a.back, &b.play, &b.back, nullptr, &a.next, nullptr, &b.next };
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, bardPages(p), bardButtons(p), p.root()), processor_(p) {
}

void PluginEditor::loaded(const String& file, const String& dir) {
    processor_.setRoot(file.isEmpty() ? dir : File(file).getParentDirectory().getFullPathName());
}

static String clock(double s) {
    int t = int(s);
    return t >= 3600 ? String::formatted("%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60)
                     : String::formatted("%d:%02d", t / 60, t % 60);
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    g.setColour(Colours::grey);
    g.drawText("root " + processor_.root(), area.removeFromTop(lineH), Justification::left);
    area.removeFromTop(lineH / 2);
    for (int d = 0; d < PluginProcessor::DECKS; d++) {
        auto i = processor_.bard().info(d);
        g.setColour(deckClr[d]);
        String shelf = i.shelves == 0 ? String("no shelves")
                                      : "shelf " + String(i.shelf + 1) + "/" + String(i.shelves) + " " + i.shelfName;
        g.drawText(String(d == 0 ? "A " : "B ") + shelf, area.removeFromTop(lineH), Justification::left);
        String book = i.books == 0 ? String("no books")
                      : i.book < 0  ? String("-")
                                    : String(i.book + 1) + "/" + String(i.books) + " " + i.bookName;
        if (i.error) book += " (read error)";
        g.setColour(i.paused ? Colours::grey : Colours::white);
        g.drawText("  " + book, area.removeFromTop(lineH), Justification::left);
        if (i.book >= 0) {
            String mark = i.mark < 0 ? String("-") : String(i.mark + 1) + "/" + String(i.marks);
            g.drawText("  " + clock(i.seconds) + " / " + clock(i.length) + "  mark " + mark +
                           (i.autoMarks ? " auto" : "") + (i.paused ? "  paused" : ""),
                       area.removeFromTop(lineH), Justification::left);
        }
        area.removeFromTop(lineH / 2);
    }
}
