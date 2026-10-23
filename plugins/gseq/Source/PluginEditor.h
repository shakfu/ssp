#pragma once

#include "PluginProcessor.h"
#include "ssp/controls/ValueButton.h"
#include "ssp/editors/BaseEditor.h"

// The grid: a row per GseqEngine row, a cell per step, with a cursor that the arrows move. The encoders edit
// the cursor's step, its row's settings (Row), or the clock (Global); docs/dev/gseq.md has the layout.
class PluginEditor : public ssp::BaseEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    void drawView(Graphics&) override;
    void onSSPTimer() override;  // the buttons' lights and the engine's cursor

protected:
    void onEncoder(unsigned enc, float v) override;
    void onEncoderSwitch(unsigned enc, bool v) override;
    void onButton(unsigned btn, bool v) override;
    void eventButton(unsigned btn, bool longPress) override;
    void eventUp(bool longPress) override;
    void eventDown(bool longPress) override;
    void eventLeft(bool longPress) override;
    void eventRight(bool longPress) override;

private:
    using Engine = gseq::GseqEngine;
    enum Mode { GRID, ROW, GLOBAL };
    enum { B_ROW, B_MUTE, B_COPY, B_PASTE, B_GLOBAL, B_CLEAR, B_REC, B_STEP, BUTTONS };

    void editStep(unsigned enc, int d, bool held);
    void editRow(unsigned enc, int d);
    void editGlobal(unsigned enc, int d, bool held);
    void pressed(unsigned enc);  // an encoder pressed and released without a turn
    void note(const String& s);  // a line shown for a moment
    String legend() const;       // what the encoders do in this mode
    void moveTo(int row, int col);
    void syncCursor();  // row_ and col_ from the engine, before any input acts on them

    PluginProcessor& processor_;
    Mode mode_ = GRID;
    int row_ = 0, col_ = 0;  // the engine's cursor, read each timer tick
    bool turned_[4] = {};    // turned while held: the release does not act
    struct Clip {
        bool full = false;
        Engine::Row row;
        Engine::Step steps[Engine::STEPS];
    } clip_;
    String note_;
    juce::uint32 noteUntil_ = 0;
    std::unique_ptr<ssp::ValueButton> buttons_[BUTTONS];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
