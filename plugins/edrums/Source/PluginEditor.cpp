#include "PluginEditor.h"

// deck A of sk-engines (drums 1, 2) warm, deck B (3, 4) cool
static const Colour drumClr[] = { Colour(255, 85, 0), Colour(255, 0, 102), Colour(0, 176, 255),
                                  Colour(122, 92, 255) };
static const char* modelNames[] = { "kick", "snare", "clap", "hat", "tom" };
static_assert(std::size(modelNames) == edrums::MODELS);

StringArray edrumsModelNames() {
    return StringArray(modelNames, edrums::MODELS);
}

StringArray edrumsRateNames() {
    StringArray names;
    for (int d = 8; d >= 2; d--) names.add("/" + String(d));
    for (int m = 1; m <= 8; m++) names.add("x" + String(m));
    return names;
}

std::vector<ssp::engine::ParamPage> edrumsPages(PluginProcessor& p) {
    std::vector<ssp::engine::ParamPage> pages;
    for (int d = 0; d < PluginProcessor::DRUMS; d++) {
        size_t first = pages.size();
        auto& k = p.drum(d);
        String n = String(d + 1) + " ";
        pages.push_back({ n + "pattern", drumClr[d],
                          { { &k.hits, 1.0f, 1.0f }, { &k.steps, 1.0f, 1.0f }, { &k.rotate, 1.0f, 1.0f },
                            { &k.rate, 1.0f, 1.0f } } });
        pages.push_back({ n + "voice", drumClr[d],
                          { { &k.model, 1.0f, 1.0f }, { &k.pitch, 0.05f, 0.005f }, { &k.decay, 0.05f, 0.01f },
                            { &k.level, 0.05f, 0.01f } } });
        pages.push_back({ n + "shape", drumClr[d],
                          { { &k.drive, 0.05f, 0.01f }, { &k.sweep, 0.05f, 0.01f }, { &k.tone, 0.05f, 0.01f },
                            { &k.bright, 0.05f, 0.01f } } });
        for (size_t i = first; i < pages.size(); i++) pages[i].group = d;
    }
    size_t globals = pages.size();
    ssp::engine::ParamPage chance{ "chance", Colours::white }, swing{ "swing", Colours::white },
        mute{ "mute", Colours::white }, voice{ "voice", Colours::white };
    for (int d = 0; d < PluginProcessor::DRUMS; d++) {
        chance.c[d] = { &p.drum(d).chance, 0.05f, 0.01f };
        swing.c[d] = { &p.drum(d).swing, 1.0f, 1.0f };
        mute.c[d] = { &p.drum(d).mute, 1.0f, 1.0f };
        voice.c[d] = { &p.drum(d).voice, 1.0f, 1.0f };
    }
    pages.push_back(chance);
    pages.push_back(swing);
    pages.push_back(mute);
    pages.push_back(voice);
    pages.push_back({ "mix", Colours::white, { { &p.route(), 1.0f, 1.0f } } });
    for (size_t i = globals; i < pages.size(); i++) pages[i].group = PluginProcessor::DRUMS;  // the global pages
    return pages;
}

std::vector<RangedAudioParameter*> edrumsButtons(PluginProcessor& p) {
    return { &p.drum(0).trig, &p.drum(1).trig, &p.drum(2).trig, &p.drum(3).trig };
}

PluginEditor::PluginEditor(PluginProcessor& p) : EngineEditor(p, edrumsPages(p), edrumsButtons(p), {}), processor_(p) {
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int rowH = 70, cell = 24, gap = 4, labelW = 220;
    static constexpr int FLASH_FRAMES = 3;
    for (int d = 0; d < PluginProcessor::DRUMS; d++) {
        auto info = processor_.edrums().info(d);
        if (info.hits != lastHits_[d]) flash_[d] = FLASH_FRAMES;
        lastHits_[d] = info.hits;

        auto row = area.removeFromTop(rowH);
        int model = std::clamp(int(processor_.drum(d).model.getValue() * (edrums::MODELS - 1) + 0.5f), 0,
                               edrums::MODELS - 1);
        auto& k = processor_.drum(d);
        bool muted = k.mute.getValue() > 0.5f;
        String name = k.voice.getValue() > 0.5f ? String(modelNames[model]) : String("trig");
        String rate = k.rate.getCurrentValueAsText();
        g.setColour(flash_[d] > 0 ? Colours::white : drumClr[d]);
        g.drawText(String(d + 1) + " " + name + (rate == "x1" ? String() : " " + rate) + (muted ? " m" : ""),
                   row.removeFromLeft(labelW), Justification::left);
        if (flash_[d] > 0) flash_[d]--;

        int y = row.getY() + (rowH - cell) / 2;
        for (int s = 0; s < info.steps; s++) {
            Rectangle<int> r(row.getX() + s * (cell + gap), y, cell, cell);
            bool on = info.onsets & (1u << s);
            g.setColour(on ? drumClr[d].withAlpha(muted ? 0.3f : 0.9f) : Colours::darkgrey.darker());
            g.fillRect(r);
            if (s == info.position) {
                g.setColour(Colours::white);
                g.drawRect(r, 2);
            }
        }
    }
}
