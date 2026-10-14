#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include "SSP.h"
#include "keys/KeyInput.h"

namespace {

constexpr int W = 1600, H = 480, MARGIN = 10, LINE = 26, OUTPUT_ROWS = 5;
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
    for (const auto& k : processor_.keys().take(sapf::KeyQueue::Clock::now())) {
        switch (text.key(k.usage, k.mods)) {
            case sapf::TextBuffer::EVALUATE: {
                int first, last;
                if (!text.block(first, last)) break;
                if (session.evaluate(text.lines(first, last))) {
                    flashFirst_ = first, flashLast_ = last;
                    flashUntil_ = juce::Time::getMillisecondCounter() + 300;
                }
                break;
            }
            case sapf::TextBuffer::STOP: session.stop(); break;
            case sapf::TextBuffer::SAVE: session.note(processor_.save().toStdString()); break;
            case sapf::TextBuffer::OPEN: session.note(processor_.open().toStdString()); break;
            default: break;
        }
    }
}

bool KeyPump::flashing(int& first, int& last) const {
    first = flashFirst_, last = flashLast_;
    return juce::Time::getMillisecondCounter() < flashUntil_;
}

PluginEditor::PluginEditor(PluginProcessor& p) : ssp::BaseView(&p, false), processor_(p), pump_(p) {
}

void PluginEditor::onEncoder(unsigned enc, float v) {
    // as the arrow keys
    if (enc == 0) processor_.text().key(v > 0 ? 0x51 : 0x52, 0);
    else if (enc == 1) processor_.text().key(v > 0 ? 0x4F : 0x50, 0);
}

void PluginEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    const auto font = mono(22);
    g.setFont(font);
    const float charW = juce::GlyphArrangement::getStringWidth(font, "M");
    const int cols = std::max(1, int((W - 2 * MARGIN) / charW));
    const int statusY = H - LINE, outputY = statusY - OUTPUT_ROWS * LINE - 4;
    const int codeRows = (outputY - 6) / LINE;
    const auto& text = processor_.text();
    const auto& lines = text.lines();

    // screen rows: each line in pieces of `cols` characters
    struct Row {
        int line, start;
    };
    std::vector<Row> rows;
    int cursorRow = 0;
    for (int r = 0; r < int(lines.size()); r++) {
        const int len = int(lines[size_t(r)].size()), pieces = std::max(1, (len + cols - 1) / cols);
        if (r == text.row()) cursorRow = int(rows.size()) + std::min(text.col() / cols, pieces - 1);
        for (int k = 0; k < pieces; k++) rows.push_back({ r, k * cols });
    }
    top_ = std::clamp(top_, cursorRow - codeRows + 1, cursorRow);

    int flashFirst, flashLast;
    const bool flashOn = pump_.flashing(flashFirst, flashLast);
    for (int i = 0; i < codeRows && top_ + i < int(rows.size()); i++) {
        const Row& row = rows[size_t(top_ + i)];
        const int y = 4 + i * LINE;
        if (flashOn && row.line >= flashFirst && row.line <= flashLast) {
            g.setColour(flash);
            g.fillRect(0, y, W, LINE);
        }
        g.setColour(juce::Colours::white);
        g.drawText(String(lines[size_t(row.line)].substr(size_t(row.start), size_t(cols))), MARGIN, y, W - 2 * MARGIN,
                   LINE, juce::Justification::left, false);
    }
    // the cursor, a bar before its column
    if (cursorRow >= top_ && cursorRow < top_ + codeRows) {
        const int x = MARGIN + int(std::round((text.col() - rows[size_t(cursorRow)].start) * charW));
        g.setColour(amber);
        g.fillRect(x - 1, 4 + (cursorRow - top_) * LINE + 2, 3, LINE - 4);
    }

    g.setColour(juce::Colour(60, 60, 60));
    g.drawHorizontalLine(outputY - 3, 0.f, float(W));
    const auto out = processor_.session().output(OUTPUT_ROWS);
    for (int i = 0; i < int(out.size()); i++) {
        g.setColour(out[size_t(i)].error ? juce::Colours::red : dim);
        g.drawText(String(out[size_t(i)].text.substr(0, size_t(cols))), MARGIN, outputY + i * LINE, W - 2 * MARGIN,
                   LINE, juce::Justification::left, false);
    }

    const bool usb = ssp::KeyInput::keyboardConnected();
    String status = "sapf " + String(JucePlugin_VersionString) + "   " + stateText(processor_.session()) +
                    "   dsp " + String(int(processor_.load() * 100.f)) + "%   keys " +
                    (usb ? "usb" : "tcp :" + String(SAPF_KEY_PORT)) + "   line " + String(text.row() + 1) +
                    "   Ctrl-Enter run  Ctrl-. stop  Ctrl-S save  Ctrl-O open";
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
