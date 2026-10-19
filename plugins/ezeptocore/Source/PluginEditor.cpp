#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, p.pages(), p.buttons(), PluginProcessor::CARD_DIR), processor_(p) {}

void PluginEditor::loaded(const String& file, const String& dir) {
    processor_.loadCard(file.isNotEmpty() ? file : dir);
}

String PluginEditor::browseFrom() {
    String card = processor_.ezep().folder();
    return card.isNotEmpty() && File(card).isDirectory() ? card : String(PluginProcessor::CARD_DIR);
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    static const char* const triggers[] = { "kick", "snare", "hihat", "random" };  // TRIGGER_MODE_
    auto& e = processor_.ezep();
    auto s = e.status();
    String card = e.folder();
    String message = String::fromUTF8(e.message().c_str());
    g.setColour(Colours::white);
    g.drawText(card.isEmpty() ? String("no card: Load a sample manager folder") : File(card).getFileName(),
               area.removeFromTop(lineH), Justification::left);
    if (message != "Ready") {
        g.setColour(Colours::orange);
        g.drawFittedText(message, area.removeFromTop(lineH * 2), Justification::topLeft, 2, 1.0f);
    }
    if (!s.loaded) return;
    g.setColour(Colours::white);
    g.drawText("bank " + String(s.bank + 1) + "  sample " + String(s.slot + 1) + "  slice " + String(s.slice + 1),
               area.removeFromTop(lineH), Justification::left);
    g.drawText(String(s.tempo) + " bpm  " + (s.stopped ? "stopped" : "playing"), area.removeFromTop(lineH),
               Justification::left);
    g.setColour(Colours::grey);
    g.drawText("trig " + String(triggers[s.triggerMode & 3]) + "  clock div " + String(s.division + 1) + "/7",
               area.removeFromTop(lineH), Justification::left);
    String fx;
    for (int i = 0; i < 16; i++)
        if (s.effects & (1u << i)) fx << " " << (i + 1);
    g.drawText("grimoire " + String(s.rune + 1) + "  fx" + (fx.isEmpty() ? String(" -") : fx),
               area.removeFromTop(lineH), Justification::left);
}
