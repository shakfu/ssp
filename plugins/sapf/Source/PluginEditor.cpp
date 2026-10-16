#include "PluginEditor.h"

#include <algorithm>

#include "SSP.h"
#include "keys/CodeText.h"
#include "keys/KeyInput.h"

namespace {

constexpr int W = 1600, H = 480, MARGIN = 10, LINE = 26, OUTPUT_ROWS = 5;
constexpr int STATUS_Y = H - LINE, OUTPUT_Y = STATUS_Y - OUTPUT_ROWS * LINE - 4;
// the meters: two groups of 2 bars right of the output pane, each after its label
// 32: MonoVuMeter's bar is METER_W / 2 - 10 px wide, 6 px here
constexpr int METER_W = 32, METER_LABEL_W = 44,
              METER_GROUP_W = METER_LABEL_W + PluginProcessor::METERED * METER_W + 16;
// the last bar ends RIGHT_CLEAR from the edge: Synthor draws its menu letters over the last ~46 px
constexpr int RIGHT_CLEAR = 60;
constexpr int METERS_X = W - RIGHT_CLEAR - (2 * METER_GROUP_W - 16);  // 16: the gap after a group
// out, then in; in is 4 px closer to out than a group's gap, to clear the menu letters
constexpr int OUT_X = METERS_X, IN_X = METERS_X + METER_GROUP_W - 4;
const juce::Colour amber(255, 214, 140), flash(30, 50, 90), dim(150, 150, 150);

juce::Font mono(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

juce::String stateText(const sapf::Session& s) {
    switch (s.state()) {
        case sapf::Session::State::LOADING: return "loading the prelude";
        case sapf::Session::State::IDLE: return "ready";
        case sapf::Session::State::RUNNING: return "evaluating";
        default: return s.error();
    }
}

}  // namespace

void KeyPump::tick() {
    ssp::KeyInput::setTarget(&processor_.keys());  // also marks this editor as on screen
    auto& text = processor_.text();
    auto& session = processor_.session();
    auto& picker = processor_.picker();
    for (const auto& k : processor_.keys().take(ssp::KeyQueue::Clock::now())) {
        if (picker.shown()) {  // Ctrl-O's list takes the keys until it closes
            switch (picker.key(k.usage)) {
                case sapf::FilePicker::OPEN:
                    picker.hide();
                    session.note(processor_.open(picker.names()[size_t(picker.selected())]).toStdString());
                    break;
                case sapf::FilePicker::CANCEL: picker.hide(); break;
                default: break;
            }
            continue;
        }
        switch (text.key(k.usage, k.mods)) {
            case ssp::TextBuffer::EVALUATE: {
                int first, last;
                if (!text.block(first, last)) break;
                if (session.evaluate(text.lines(first, last))) {
                    flashFirst_ = first, flashLast_ = last;
                    flashUntil_ = juce::Time::getMillisecondCounter() + 300;
                }
                break;
            }
            case ssp::TextBuffer::STOP: session.stop(); break;
            case ssp::TextBuffer::SAVE: session.note(processor_.save().toStdString()); break;
            case ssp::TextBuffer::OPEN: {
                std::vector<std::string> names;
                for (const auto& n : processor_.files()) names.push_back(n.toStdString());
                picker.show(names, processor_.currentFile().toStdString());
                break;
            }
            default: break;
        }
    }
}

bool KeyPump::flashing(int& first, int& last) const {
    first = flashFirst_, last = flashLast_;
    return juce::Time::getMillisecondCounter() < flashUntil_;
}

PluginEditor::PluginEditor(PluginProcessor& p) : ssp::BaseView(&p, false), processor_(p), pump_(p) {
    constexpr int C = PluginProcessor::METERED;
    for (int i = 0; i < 2 * C; i++) {
        auto& m = meters_[i];
        m.init(String(i % C + 1));
        m.labelColour(i < C ? dim : amber);
        m.active(true);
        m.enabled(true);
        const int group = i / C;
        m.setBounds((group ? OUT_X : IN_X) + METER_LABEL_W + (i % C) * METER_W, OUTPUT_Y, METER_W,
                    STATUS_Y - OUTPUT_Y - 2);
        addAndMakeVisible(m);
    }
}

void PluginEditor::onSSPTimer() {
    pump_.tick();
    constexpr int C = PluginProcessor::METERED;
    for (int c = 0; c < C; c++) {
        meters_[c].level(processor_.inLevel(c));
        meters_[C + c].level(processor_.outLevel(c));
    }
}

void PluginEditor::onEncoder(unsigned enc, float v) {
    // as the arrow keys; in Ctrl-O's list, encoder 1 moves the selection
    if (processor_.picker().shown()) {
        if (enc == 0) processor_.picker().move(v > 0 ? 1 : -1);
        return;
    }
    if (enc == 0) processor_.text().key(v > 0 ? 0x51 : 0x52, 0);
    else if (enc == 1) processor_.text().key(v > 0 ? 0x4F : 0x50, 0);
}

void PluginEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    const auto font = mono(22);
    g.setFont(font);
    const float charW = juce::GlyphArrangement::getStringWidth(font, "M");
    const int outCols = std::max(1, int((METERS_X - 2 * MARGIN) / charW));  // the output pane, left of the meters
    const int statusY = STATUS_Y, outputY = OUTPUT_Y;
    const int codeRows = (outputY - 6) / LINE;
    const auto& text = processor_.text();

    const auto& picker = processor_.picker();
    if (picker.shown()) {  // Ctrl-O's list in place of the code, the selection kept in view
        const auto& names = picker.names();
        g.setColour(amber);
        g.drawText(names.empty() ? "no .sapf or .txt files in " + PluginProcessor::filesDir()
                                 : "open (Enter), or Esc: " + PluginProcessor::filesDir(),
                   MARGIN, 4, W - 2 * MARGIN, LINE, juce::Justification::left, false);
        const int shown = codeRows - 1, first = std::clamp(picker.selected() - shown / 2, 0,
                                                            std::max(0, int(names.size()) - shown));
        for (int i = 0; i < shown && first + i < int(names.size()); i++) {
            const int y = 4 + (i + 1) * LINE;
            if (first + i == picker.selected()) {
                g.setColour(flash);
                g.fillRect(0, y, W, LINE);
            }
            g.setColour(juce::Colours::white);
            g.drawText(String(names[size_t(first + i)]), MARGIN + 2 * int(charW), y, W - 2 * MARGIN, LINE,
                       juce::Justification::left, false);
        }
    } else {
        int flashFirst, flashLast;
        if (!pump_.flashing(flashFirst, flashLast)) flashFirst = 0, flashLast = -1;
        ssp::drawCode(g, text, { MARGIN, 4, W - 2 * MARGIN, codeRows * LINE }, LINE, top_, flashFirst, flashLast);
    }

    g.setColour(juce::Colour(60, 60, 60));
    g.drawHorizontalLine(outputY - 3, 0.f, float(W));
    const auto out = processor_.session().output(OUTPUT_ROWS);
    for (int i = 0; i < int(out.size()); i++) {
        g.setColour(out[size_t(i)].error ? juce::Colours::red : dim);
        g.drawText(String(out[size_t(i)].text.substr(0, size_t(outCols))), MARGIN, outputY + i * LINE,
                   METERS_X - 2 * MARGIN, LINE, juce::Justification::left, false);
    }
    g.setColour(dim);
    g.drawText("in", IN_X, outputY, METER_LABEL_W, statusY - outputY, juce::Justification::centred, false);
    g.setColour(amber);
    g.drawText("out", OUT_X, outputY, METER_LABEL_W, statusY - outputY,
               juce::Justification::centred, false);

    const bool usb = ssp::KeyInput::keyboardConnected();
    const auto& session = processor_.session();
    // dsp: the render thread's share of real time; late: sound the callback lacked, since the start
    const uint64_t late = session.lateFrames();
    String status = "sapf " + String(JucePlugin_VersionString) + "   " + stateText(session) + "   dsp " +
                    String(int(session.renderLoad() * 100.f)) + "%" + (session.renderRealtime() ? " rt" : "") +
                    (late ? "   late " + String(int(late * 1000 / uint64_t(processor_.sampleRate()))) + " ms" : "") +
                    "   keys " +
                    (usb ? "usb" : "tcp :" + String(SAPF_KEY_PORT)) + "   " + processor_.currentFile() + ":" +
                    String(text.row() + 1) +
                    "   C-Enter run  C-. stop  C-S save  C-O open";
    g.setColour(amber);
    g.drawText(status, MARGIN, statusY, W - 2 * MARGIN, LINE, juce::Justification::left, false);
}

PluginMiniEditor::PluginMiniEditor(PluginProcessor& p) : ssp::BaseView(&p, true), processor_(p), pump_(p) {
}

void PluginMiniEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setFont(mono(20));
    const int w = SSP_COMPACT_WIDTH, h = SSP_COMPACT_HEIGHT, lh = 24;
    g.setColour(amber);
    g.drawText("sapf  " + stateText(processor_.session()), 10, 6, w - 20, lh, juce::Justification::left);
    const auto out = processor_.session().output(size_t((h - 2 * lh) / lh));
    for (int i = 0; i < int(out.size()); i++) {
        g.setColour(out[size_t(i)].error ? juce::Colours::red : dim);
        g.drawText(String(out[size_t(i)].text), 10, 6 + (i + 1) * lh, w - 20, lh, juce::Justification::left, true);
    }
}
