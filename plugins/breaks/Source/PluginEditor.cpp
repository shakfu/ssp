#include "PluginEditor.h"

PluginEditor::PluginEditor(PluginProcessor& p)
    : EngineEditor(p, p.pages(), p.buttons(), PluginProcessor::CARD_DIR), processor_(p) {}

void PluginEditor::loaded(const String& file, const String& dir) {
    processor_.loadCard(file.isNotEmpty() ? file : dir);
}

// Tempo follows the tempo playing (Tap, In 1, the card's), except for 1 s after it is turned.
void PluginEditor::onSSPTimer() {
    EngineEditor::onSSPTimer();
    auto& p = processor_.tempo();
    float v = p.convertFrom0to1(p.getValue());
    uint32_t now = Time::getMillisecondCounter();
    if (v != tempoSeen_) {
        if (v != tempoSet_) tempoTurned_ = now;
        tempoSeen_ = v;
    }
    auto s = processor_.brks().status();
    if (!s.loaded || now - tempoTurned_ < 1000 || std::lround(v) == long(s.tempo)) return;
    tempoSet_ = tempoSeen_ = float(s.tempo);
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(tempoSet_));
    p.endChangeGesture();
}

void PluginEditor::step(int dir) {
    processor_.brks().stepBank(dir);
}

String PluginEditor::browseFrom() {
    String card = processor_.brks().folder();
    return card.isNotEmpty() && File(card).isDirectory() ? card : String(PluginProcessor::CARD_DIR);
}

void PluginEditor::drawStatus(Graphics& g, Rectangle<int> area) {
    static constexpr int lineH = 30;
    static const char* const triggers[] = { "kick", "snare", "hihat", "random" };  // TRIGGER_MODE_
    auto& e = processor_.brks();
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
    g.drawText("trig " + String(triggers[s.triggerMode & 3]) + "  clock " + PluginProcessor::CLOCK_NAMES[std::min(s.division, 6u)],
               area.removeFromTop(lineH), Justification::left);
    // the Grimoire's effects (effectN-on, break_fx_toggle) and the firmware's (FX_ in definitions.h)
    static const char* const slots[16] = { "distort", "loss", "crush", "filter", "stretch", "delay", "comb", "repeat",
                                           "reverb", "pan", "pitch-", "pitch+", "reverse", "retrig", "retrig+",
                                           "tape stop" };
    static const char* const fx[16] = { "saturate", "loss", "distort", "crush", "stretch", "delay", "comb", "repeat",
                                        "gate", "reverb", "pan", "scratch", "filter", "pitch", "reverse", "tape stop" };
    auto names = [](uint16_t bits, const char* const* n) {
        StringArray a;
        for (int i = 0; i < 16; i++)
            if (bits & (1u << i)) a.add(n[i]);
        return a.isEmpty() ? String("-") : a.joinIntoString(" ");
    };
    g.drawFittedText("grimoire " + String(s.rune + 1) + ": " + names(s.allowed, slots), area.removeFromTop(lineH * 2),
                     Justification::topLeft, 2, 1.0f);
    g.drawFittedText("playing: " + names(s.effects, fx), area.removeFromTop(lineH * 2), Justification::topLeft, 2,
                     1.0f);
}
