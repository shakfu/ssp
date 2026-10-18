#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, p.pages(), {}, PluginProcessor::FONT_DIR), processor_(p) {}

void PluginEditor::loaded(const String& file, const String&) {
    if (file.isNotEmpty()) processor_.loadFont(file);
}

String PluginEditor::browseFrom() {
    String path = processor_.fluid().path();
    return path.isNotEmpty() && File(path).existsAsFile() ? path : String(PluginProcessor::FONT_DIR);
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    auto& f = processor_.fluid();
    String font = f.font();
    g.setColour(Colours::white);
    g.drawText(font.isEmpty() ? String("no soundfont: Load an .sf2") : File(font).getFileName(), area.removeFromTop(lineH),
               Justification::left);
    g.setColour(Colours::grey);
    if (f.loading()) {
        g.setColour(Colours::orange);
        g.drawText("loading " + File(f.path()).getFileName(), area.removeFromTop(lineH), Justification::left);
        g.setColour(Colours::grey);
    }
    g.drawText("voices " + String(f.voicesPlaying()), area.removeFromTop(lineH), Justification::left);
    int dropped = f.eventsDropped();
    if (dropped > 0) g.drawText("MIDI dropped " + String(dropped), area.removeFromTop(lineH), Justification::left);
    String err = f.error();
    if (err.isEmpty()) return;
    g.setColour(Colours::orange);
    g.drawFittedText(err, area.removeFromTop(lineH * 3), Justification::topLeft, 3, 1.0f);
}
