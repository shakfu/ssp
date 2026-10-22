#include "PluginEditor.h"

#include "SSP.h"

namespace {

// the log: below the page's bars (y 68-136), above the buttons
constexpr int MARGIN = 10, LOG_LINE = 24, LOG_TOP = 146, LOG_BOTTOM = 366;
// the meters: two groups of 2 bars below the status panel, each after its label
// 32: MonoVuMeter's bar is METER_W / 2 - 10 px wide, 6 px here
constexpr int METER_W = 32, METER_LABEL_W = 44,
              METER_GROUP_W = METER_LABEL_W + PluginProcessor::METERED * METER_W + 16;
// the last bar ends RIGHT_CLEAR from the edge: Synthor draws its menu letters over the last ~46 px
constexpr int RIGHT_CLEAR = 60, METERS_Y = 220;
constexpr int METERS_X = 1600 - RIGHT_CLEAR - (2 * METER_GROUP_W - 16);  // 16: the gap after a group
// out, then in; in is 4 px closer to out than a group's gap, to clear the menu letters
constexpr int OUT_X = METERS_X, IN_X = METERS_X + METER_GROUP_W - 4;
const juce::Colour amber(255, 214, 140), dim(150, 150, 150);

juce::Font mono(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

}  // namespace

PluginEditor::PluginEditor(PluginProcessor& p) : ScriptEditor(p), processor_(p) {
    constexpr int C = PluginProcessor::METERED;
    for (int i = 0; i < 2 * C; i++) {
        auto& m = meters_[i];
        m.init(String(i % C + 1));
        m.labelColour(i < C ? dim : amber);
        m.active(true);
        m.enabled(true);
        const int group = i / C;
        m.setBounds((group ? OUT_X : IN_X) + METER_LABEL_W + (i % C) * METER_W, METERS_Y, METER_W,
                    LOG_BOTTOM - METERS_Y);
        addChildComponent(m);
        m.setVisible(view_ == V_MAIN);
    }
}

void PluginEditor::setView(unsigned newView) {
    ScriptEditor::setView(newView);
    for (auto& m : meters_) m.setVisible(newView == V_MAIN);
}

void PluginEditor::onSSPTimer() {
    ScriptEditor::onSSPTimer();
    constexpr int C = PluginProcessor::METERED;
    for (int c = 0; c < C; c++) {
        meters_[c].level(processor_.inLevel(c));
        meters_[C + c].level(processor_.outLevel(c));
    }
}

void PluginEditor::drawView(Graphics& g) {
    ScriptEditor::drawView(g);
    if (view_ != V_MAIN) return;
    std::vector<ssp::engine::ScriptEngine::OutputLine> out;
    processor_.sapf().output(out, size_t((LOG_BOTTOM - LOG_TOP) / LOG_LINE));
    g.setFont(mono(18));
    for (size_t i = 0; i < out.size(); i++) {
        g.setColour(out[i].error ? juce::Colours::red : dim);
        g.drawText(String(out[i].text), MARGIN, LOG_BOTTOM - int(out.size() - i) * LOG_LINE, panelX - 2 * MARGIN,
                   LOG_LINE, juce::Justification::left, false);
    }
    g.setFont(mono(20));
    g.setColour(dim);
    g.drawText("in", IN_X, METERS_Y, METER_LABEL_W, LOG_BOTTOM - METERS_Y, juce::Justification::centred, false);
    g.setColour(amber);
    g.drawText("out", OUT_X, METERS_Y, METER_LABEL_W, LOG_BOTTOM - METERS_Y, juce::Justification::centred, false);
}

PluginMiniEditor::PluginMiniEditor(PluginProcessor& p) : ssp::BaseView(&p, true), processor_(p) {
}

void PluginMiniEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setFont(mono(20));
    const int w = SSP_COMPACT_WIDTH, h = SSP_COMPACT_HEIGHT, lh = 24;
    const auto& sapf = processor_.sapf();
    g.setColour(amber);
    g.drawText("sapf  " + StringArray::fromLines(sapf.status())[0], 10, 6, w - 20, lh, juce::Justification::left);
    std::vector<ssp::engine::ScriptEngine::OutputLine> out;
    sapf.output(out, size_t((h - 2 * lh) / lh));
    for (size_t i = 0; i < out.size(); i++) {
        g.setColour(out[i].error ? juce::Colours::red : dim);
        g.drawText(String(out[i].text), 10, 6 + int(i + 1) * lh, w - 20, lh, juce::Justification::left, true);
    }
}
