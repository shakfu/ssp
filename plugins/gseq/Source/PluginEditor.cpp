#include "PluginEditor.h"

#include <algorithm>

using Engine = gseq::GseqEngine;

namespace {

// the grid: a label column, then STEPS cells, clear of the menu letters Synthor draws at the right edge
constexpr int GX = 150, CW = 43, GY = 36, RH = 42, INFO_Y = 384;
const juce::Colour amber(255, 214, 140), dim(150, 150, 150), off(38, 38, 38), bar(255, 140, 0), cyan(0, 200, 255);

const char* const DIVISION_NAMES[Engine::DIVISIONS] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
const char* const DURATION_NAMES[Engine::DURATIONS] = { "1/8", "1/4", "3/8", "1/2", "5/8", "3/4", "7/8", "1",
                                                        "1.5", "2",   "3",   "4",   "6",   "8",   "12",  "16" };

juce::Font mono(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

// C4 is MIDI 60, as for alda
String noteName(int n) {
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String(names[n % 12]) + String(n / 12 - 1);
}

float value(const RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

void nudge(RangedAudioParameter& p, float by) {
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(p.getNormalisableRange().snapToLegalValue(value(p) + by)));
    p.endChangeGesture();
}

}  // namespace

PluginEditor::PluginEditor(PluginProcessor& p) : BaseEditor(&p), processor_(p) {
    static const char* const labels[BUTTONS] = { "Row", "Mute", "Copy", "Paste", "Global", "Clear", "Rec", "Step" };
    for (int b = 0; b < BUTTONS; b++) {
        const bool red = b == B_CLEAR || b == B_REC || b == B_STEP;
        buttons_[b] = std::make_unique<ssp::ValueButton>(labels[b], nullptr, 24, red ? Colours::red : cyan);
        setButtonBounds(*buttons_[b], unsigned(b / 4), unsigned(b % 4));
        addAndMakeVisible(*buttons_[b]);
    }
    leftBtn_.label("STP-");
    rightBtn_.label("STP+");
    upBtn_.label("ROW-");
    downBtn_.label("ROW+");
    addAndMakeVisible(leftBtn_);
    addAndMakeVisible(rightBtn_);
    syncCursor();
    setSize(1600, 480);
}

void PluginEditor::onSSPTimer() {
    BaseEditor::onSSPTimer();
    buttons_[B_ROW]->value(mode_ == ROW);
    buttons_[B_GLOBAL]->value(mode_ == GLOBAL);
    auto& g = processor_.gseq();
    buttons_[B_MUTE]->value(g.row(row_).mute);
    // Rec blinks while armed, lights while recording
    const auto rec = g.record();
    buttons_[B_REC]->value(rec == Engine::RECORDING ||
                           (rec == Engine::ARMED && juce::Time::getMillisecondCounter() / 250 % 2));
    buttons_[B_STEP]->value(g.stepEntry());
    syncCursor();
}

// the engine holds the cursor: step entry and the Launchpad move it too
void PluginEditor::syncCursor() {
    auto& g = processor_.gseq();
    row_ = g.cursorRow();
    col_ = std::min(g.cursorCol(), g.row(row_).length - 1);
}

void PluginEditor::note(const String& s) {
    note_ = s;
    noteUntil_ = juce::Time::getMillisecondCounter() + 2500;
}

void PluginEditor::moveTo(int row, int col) {
    row_ = std::clamp(row, 0, Engine::ROWS - 1);
    col_ = std::clamp(col, 0, processor_.gseq().row(row_).length - 1);
    processor_.gseq().setCursor(row_, col_);
}

void PluginEditor::eventUp(bool longPress) {
    syncCursor();
    moveTo(longPress ? 0 : row_ - 1, col_);
}

void PluginEditor::eventDown(bool longPress) {
    syncCursor();
    moveTo(longPress ? Engine::ROWS - 1 : row_ + 1, col_);
}

void PluginEditor::eventLeft(bool longPress) {
    syncCursor();
    moveTo(row_, longPress ? 0 : col_ - 1);
}

void PluginEditor::eventRight(bool longPress) {
    syncCursor();
    moveTo(row_, longPress ? Engine::STEPS : col_ + 1);
}

void PluginEditor::onEncoder(unsigned enc, float v) {
    if (enc > 3 || v == 0.0f) return;
    syncCursor();
    const int d = v > 0.0f ? 1 : -1;
    const bool held = encoderState_[enc];
    if (held) turned_[enc] = true;
    switch (mode_) {
    case GRID: editStep(enc, d, held); break;
    case ROW: editRow(enc, d); break;
    case GLOBAL: editGlobal(enc, d, held); break;
    }
}

void PluginEditor::onEncoderSwitch(unsigned enc, bool v) {
    BaseEditor::onEncoderSwitch(enc, v);
    if (enc > 3) return;
    if (v) {
        turned_[enc] = false;
        return;
    }
    syncCursor();
    if (!turned_[enc]) pressed(enc);
}

// Grid: 1 note (held: octaves; a turn sets the step on), press on/off; 2 velocity (held: by 10); 3 duration;
// 4 the row's length, press its division.
void PluginEditor::editStep(unsigned enc, int d, bool held) {
    auto& g = processor_.gseq();
    if (enc == 3) {
        auto row = g.row(row_);
        row.length = uint8_t(std::clamp(row.length + d, 1, Engine::STEPS));
        g.setRow(row_, row);
        moveTo(row_, col_);
        return;
    }
    auto s = g.step(row_, col_);
    if (enc == 0) {
        s.note = uint8_t(std::clamp(s.note + d * (held ? 12 : 1), 0, 127));
        s.on = true;
    } else if (enc == 1) {
        s.velocity = uint8_t(std::clamp(s.velocity + d * (held ? 10 : 1), 1, 127));
    } else {
        s.duration = uint8_t(std::clamp(s.duration + d, 0, Engine::DURATIONS - 1));
    }
    g.setStep(row_, col_, s);
}

// Row: 1 MIDI channel, 2 CV voice (past those other rows hold), 3 velocity CV (off, held, gated), 4 division
void PluginEditor::editRow(unsigned enc, int d) {
    auto& g = processor_.gseq();
    auto row = g.row(row_);
    if (enc == 0) {
        row.channel = uint8_t(std::clamp(row.channel + d, 0, 16));
    } else if (enc == 1) {
        int v = row.voice + d;
        auto held = [&](int voice) {
            for (int r = 0; r < Engine::ROWS; r++)
                if (r != row_ && g.row(r).voice == voice) return r + 1;
            return 0;
        };
        while (v >= 1 && v <= Engine::VOICES && held(v)) {
            note("cv" + String(v) + " is row " + String(held(v)) + "'s");
            v += d;
        }
        if (v < 0 || v > Engine::VOICES) return;
        row.voice = uint8_t(v);
    } else if (enc == 2) {
        const int vel = std::clamp((row.velocityCv ? 1 + row.velocityGated : 0) + d, 0, 2);  // off, held, gated
        row.velocityCv = vel > 0;
        row.velocityGated = vel == 2;
    } else {
        row.division = uint8_t(std::clamp(row.division + d, 0, Engine::DIVISIONS - 1));
    }
    g.setRow(row_, row);
}

// Global: 1 tempo (held: by 10), 2 swing, 3 clock source, 4 the clock input's pulses a quarter
void PluginEditor::editGlobal(unsigned enc, int d, bool held) {
    RangedAudioParameter* params[4] = { &processor_.tempo(), &processor_.swing(), &processor_.clock(),
                                        &processor_.ppqn() };
    nudge(*params[enc], float(d * (enc == 0 && held ? 10 : 1)));
}

void PluginEditor::pressed(unsigned enc) {
    auto& g = processor_.gseq();
    if (mode_ != GRID) return;
    if (enc == 0) {
        auto s = g.step(row_, col_);
        s.on = !s.on;
        g.setStep(row_, col_, s);
    } else if (enc == 3) {
        auto row = g.row(row_);
        row.division = uint8_t((row.division + 1) % Engine::DIVISIONS);
        g.setRow(row_, row);
    }
}

void PluginEditor::onButton(unsigned btn, bool v) {
    BaseEditor::onButton(btn, v);
    if (btn == B_COPY || btn == B_PASTE || btn == B_CLEAR) buttons_[btn]->value(v);  // Rec and Step: onSSPTimer
}

void PluginEditor::eventButton(unsigned btn, bool longPress) {
    BaseEditor::eventButton(btn, longPress);
    syncCursor();
    auto& g = processor_.gseq();
    switch (btn) {
    case B_ROW: mode_ = mode_ == ROW ? GRID : ROW; break;
    case B_GLOBAL: mode_ = mode_ == GLOBAL ? GRID : GLOBAL; break;
    case B_MUTE: {
        auto row = g.row(row_);
        row.mute = !row.mute;
        g.setRow(row_, row);
        break;
    }
    case B_COPY:
        clip_.full = true;
        clip_.row = g.row(row_);
        for (int s = 0; s < Engine::STEPS; s++) clip_.steps[s] = g.step(row_, s);
        note("copied row " + String(row_ + 1));
        break;
    case B_PASTE: {  // the steps, length and division; the row keeps its MIDI channel and CV voice
        if (!clip_.full) {
            note("nothing copied");
            break;
        }
        auto row = g.row(row_);
        row.length = clip_.row.length;
        row.division = clip_.row.division;
        g.setRow(row_, row);
        for (int s = 0; s < Engine::STEPS; s++) g.setStep(row_, s, clip_.steps[s]);
        moveTo(row_, col_);
        note("pasted into row " + String(row_ + 1));
        break;
    }
    case B_REC:  // arms the cursor's row; again, stops the take
        g.setStepEntry(false);
        g.toggleRecord();
        break;
    case B_STEP:
        g.setStepEntry(!g.stepEntry());
        note(g.stepEntry() ? "step entry: each note at the cursor, which moves on" : "step entry off");
        break;
    case B_CLEAR:
        if (!longPress) {
            note("hold Clear to clear row " + String(row_ + 1));
            break;
        }
        for (int s = 0; s < Engine::STEPS; s++) g.setStep(row_, s, Engine::Step());
        note("cleared row " + String(row_ + 1));
        break;
    default: break;
    }
}

String PluginEditor::legend() const {
    auto& g = processor_.gseq();
    const auto row = g.row(row_);
    switch (mode_) {
    case ROW:
        return "E1 midi " + (row.channel ? "ch" + String(row.channel) : String("off")) + "   E2 cv " +
               (row.voice ? String(row.voice) : String("off")) + "   E3 vel cv " +
               (!row.velocityCv ? "off" : row.velocityGated ? "gated" : "held") + "   E4 " +
               DIVISION_NAMES[row.division];
    case GLOBAL:
        return "E1 tempo " + String(int(value(processor_.tempo()))) + "   E2 swing " +
               String(int(value(processor_.swing()))) + "%   E3 clock " + processor_.clock().getCurrentValueAsText() +
               "   E4 ppqn " + String(int(value(processor_.ppqn())));
    default:
        return "E1 note, press on/off   E2 velocity   E3 duration   E4 length " + String(row.length) +
               ", press " + DIVISION_NAMES[row.division];
    }
}

void PluginEditor::drawView(Graphics& g) {
    BaseEditor::drawView(g);
    auto& e = processor_.gseq();
    g.setFont(mono(18));
    g.setColour(amber);
    String head = String(e.bpm(), 1) + " bpm   " + processor_.clock().getCurrentValueAsText() + "   swing " +
                  String(int(value(processor_.swing()))) + "%";
    if (unsigned dropped = e.midiDropped()) head << "   midi dropped " << String(dropped);
    const auto rec = e.record();
    if (rec != Engine::IDLE)
        head << "   REC row " << String(e.recordRow() + 1) << (rec == Engine::ARMED ? " armed" : " recording");
    else if (e.stepEntry())
        head << "   STEP";
    g.drawText(head, 420, 6, 1050, 24, Justification::left, false);

    for (int r = 0; r < Engine::ROWS; r++) {
        const auto row = e.row(r);
        const int y = GY + r * RH;
        g.setFont(mono(16));
        g.setColour(row.mute ? Colours::red : r == row_ ? amber : dim);
        const bool silent = !row.channel && !row.voice;  // its steps reach no output
        String route = String(r + 1) + (row.channel ? " ch" + String(row.channel) : String()) +
                       (row.voice ? " cv" + String(row.voice) : String());
        if (silent) route << " no output";
        g.drawText(route, 6, y + 2, GX - 10, 18, Justification::left, false);
        g.drawText(row.mute ? String("mute") : String(row.length) + " x " + DIVISION_NAMES[row.division], 6, y + 21,
                   GX - 10, 18, Justification::left, false);

        const float alpha = row.mute ? 0.35f : 1.0f;
        for (int s = 0; s < row.length; s++) {
            const auto st = e.step(r, s);
            const Rectangle<int> cell(GX + s * CW + 1, y + 1, CW - 2, RH - 2);
            if (!st.on) {
                g.setColour(off.withMultipliedAlpha(alpha));
                g.fillRect(cell);
                continue;
            }
            const auto fill = (silent ? dim : amber).withMultipliedBrightness(0.35f + 0.65f * st.velocity / 127.0f);
            g.setColour(fill.withMultipliedAlpha(alpha));
            g.fillRect(cell);
            // the note fills the cell above the bar, dark on a light fill and light on a dark one
            g.setFont(mono(24).boldened());
            g.setColour((fill.getPerceivedBrightness() > 0.5f ? Colours::black : Colours::white).withMultipliedAlpha(alpha));
            g.drawFittedText(noteName(st.note), cell.withTrimmedBottom(5), Justification::centred, 1, 0.6f);
            if (row.voice && st.note > 120) {  // beyond +5 V on CV
                g.setColour(Colours::red);
                g.drawRect(cell, 2);
            }
        }
        // the durations, across the cells they cover to the row's end: after the cells, which would cover them
        g.setColour(bar.withMultipliedAlpha(alpha));
        for (int s = 0; s < row.length; s++) {
            const auto st = e.step(r, s);
            if (!st.on) continue;
            const float steps = std::min(Engine::DURATION_EIGHTHS[st.duration] / 8.0f, float(row.length - s));
            g.fillRect(float(GX + s * CW + 1), float(y + RH - 6), steps * CW - 2.0f, 5.0f);
        }
        const int at = e.playing(r);
        if (at >= 0 && at < row.length) {
            g.setColour(Colours::white);
            g.drawRect(GX + at * CW, y, CW, RH, 2);
        }
    }
    g.setColour(cyan);
    g.drawRect(GX + col_ * CW - 1, GY + row_ * RH - 1, CW + 2, RH + 2, 3);

    const auto st = e.step(row_, col_);
    g.setFont(mono(18));
    g.setColour(Colours::white);
    g.drawText("row " + String(row_ + 1) + " step " + String(col_ + 1) + "   " + (st.on ? "on " : "off ") +
                   noteName(st.note) + "   vel " + String(st.velocity) + "   dur " + DURATION_NAMES[st.duration],
               10, INFO_Y, 880, 24, Justification::left, false);
    g.setColour(mode_ == GRID ? dim : cyan);
    g.drawText(String(mode_ == ROW ? "row: " : mode_ == GLOBAL ? "global: " : "") + legend(), 10, INFO_Y + 30, 880,
               24, Justification::left, false);
    if (juce::Time::getMillisecondCounter() < noteUntil_) {
        g.setColour(amber);
        g.drawText(note_, 10, INFO_Y + 60, 880, 24, Justification::left, false);
    }
}
