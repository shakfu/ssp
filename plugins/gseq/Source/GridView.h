#pragma once

#include <mutex>

#include "GseqEngine.h"
#include "monome/Grid.h"

namespace gseq {

// gseq on a 16 x 8 monome grid (docs/dev/monome.md). The whole grid is a window of 8 rows by 16 steps, one
// of two pages over the 32 steps; grid row y is gseq row y + 1, as on screen. A key moves the cursor there
// and switches the step on or off; a step switched on takes the last note played (GseqEngine::lastNote()),
// else C4. In roll mode (docs/dev/gseq-roll.md) the rows are 8 pitches, highest at the top, and a key adds or
// removes the note there. The page, and roll's pitches, follow the cursor. Mutes, Rec and Step stay on the
// SSP. JUCE-free: the owner passes keys in and sends frames.
//
// press() runs on the grid's thread, render() on another thread that is not audio; a mutex guards the
// view's state. Every engine call is one the engine allows from these threads.
class GridView {
public:
    static constexpr int STEPS = ssp::monome::COLS, PITCHES = ssp::monome::ROWS;
    static constexpr int HIGHEST_PITCH_LOW = 127 - PITCHES + 1;  // roll: the window's lowest pitch, at most
    // LED levels, 0-15
    static constexpr uint8_t OFF = 2;          // a step off, within its row's length
    static constexpr uint8_t SILENT = 4;       // a step on in a row that is muted or has no output
    static constexpr uint8_t ON_LOW = 7, ON_HIGH = 12;  // a step on, velocity 1 .. 127
    static constexpr uint8_t PLAY_OFF = 5, PLAY_ON = 15;  // the playing step, off or on
    static constexpr uint8_t HOLD = 4;         // roll: a step a note holds after its start
    static constexpr uint8_t C_ROW = 2, PITCH_ROW = 1;  // roll: empty, on a C and on other pitches

    explicit GridView(GseqEngine& e) : e_(e) {}

    void press(const ssp::monome::Key& k);
    // Builds the frame; moves the window to show the cursor when the cursor has moved since the last call.
    // blink: the cursor's phase, lit or not.
    void render(ssp::monome::Frame& f, bool blink);

    int left() const;      // the window's first step: 0 or 16
    int pitchLow() const;  // roll: the window's lowest pitch, its bottom row

    static uint8_t onLevel(int velocity) { return uint8_t(ON_LOW + (ON_HIGH - ON_LOW) * velocity / 127); }

private:
    void showCursor();
    void renderGrid(ssp::monome::Frame& f, bool blink);
    void renderRoll(ssp::monome::Frame& f, bool blink);

    GseqEngine& e_;
    mutable std::mutex lock_;
    int left_ = 0;
    int pitchLow_ = 60;  // roll: pitches pitchLow_ .. pitchLow_ + 7, C4 .. G4 at first
    int cursorSeen_ = -1;
};

}  // namespace gseq
