#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, p.pages(), {}, PluginProcessor::CART_DIR), processor_(p) {
    leftBtn_.label("PG-");
    rightBtn_.label("PG+");
}

void PluginEditor::step(int dir) {
    processor_.stepVoice(dir);
}

void PluginEditor::loaded(const String& file, const String&) {
    if (file.isNotEmpty()) processor_.loadCartridge(file);
}

String PluginEditor::browseFrom() {
    String path = processor_.cartridgePath();
    return path.isNotEmpty() && File(path).existsAsFile() ? path : String(PluginProcessor::CART_DIR);
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    String path = processor_.cartridgePath();
    g.setColour(Colours::white);
    g.drawText(path.isEmpty() ? String("no cartridge: Load a .syx") : File(path).getFileName(), area.removeFromTop(lineH),
               Justification::left);
    int v = processor_.voice();
    g.setColour(Colours::orange);
    g.drawText(String(v + 1).paddedLeft('0', 2) + " " + processor_.voiceName(v), area.removeFromTop(lineH),
               Justification::left);
    g.setColour(Colours::grey);
    g.drawText("notes " + String(processor_.dexed().notesPlaying()), area.removeFromTop(lineH), Justification::left);
    int dropped = processor_.dexed().eventsDropped();
    if (dropped > 0) g.drawText("MIDI dropped " + String(dropped), area.removeFromTop(lineH), Justification::left);
    String status = processor_.cartridgeStatus();
    if (status.isEmpty()) return;
    g.setColour(Colours::orange);
    g.drawFittedText(status, area.removeFromTop(lineH * 2), Justification::topLeft, 2, 1.0f);
}
