#pragma once

#include <mutex>

#include "GseqEngine.h"
#include "launchpad/Launchpad.h"

namespace gseq {

// gseq on a Launchpad Mini MK3 or X; docs/dev/gseq-launchpad.md has the layout and the capture rules. The top 6
// pad rows are a window of 6 rows by 8 steps onto the grid (cursor pads), the bottom 2 a piano keyboard
// from C to C (key pads). Side keys mute the window's rows and set the keyboard's octave; the top row
// moves the window and holds Rec, Step and Follow. In roll mode (docs/dev/gseq-roll.md) all 8 pad rows are
// pitches, with no keyboard; the arrows move them a semitone, and the side keys do nothing. JUCE-free: the owner
// passes events in and sends frames.
//
// press() runs on the MIDI input thread, render() on another thread that is not audio; a mutex guards the
// view's state. Every engine call is one the engine allows from these threads.
class LaunchpadView {
public:
    static constexpr int WINDOW_ROWS = 6, WINDOW_STEPS = 8, KEY_ROWS = 2;
    static constexpr int ROLL_ROWS = 8;                            // roll: the whole grid is pitches, no keyboard
    static constexpr int VELOCITY = 100;                           // key pads on a model without velocity
    static constexpr int LOWEST = 0, HIGHEST = 108, START = 60;    // the keyboard's first C: C-1 .. C8, C4
    static constexpr int HIGHEST_PITCH_LOW = 127 - ROLL_ROWS + 1;  // roll: the window's lowest pitch, at most

    explicit LaunchpadView(GseqEngine& e) : e_(e) {}

    void press(const ssp::launchpad::Event& ev);
    // Builds the frame; moves the window to show the cursor when the cursor has moved since the last call.
    void render(ssp::launchpad::Frame& f);

    // the window's first row and step, and the keyboard's lowest note: for tests and the screen
    int top() const;
    int pitchLow() const;  // roll: the window's lowest pitch
    int left() const;
    int octave() const;
    bool follow() const;

    // the note a key pad plays, or -1 for the black row's gaps
    static int keyNote(int x, int y, int base);

private:
    void cursorPad(int row, int col, int velocity);  // velocity 0: the model has none
    void rollPad(int pitch, int col, int velocity);  // velocity 0: the model has none
    void renderGrid(ssp::launchpad::Frame& f);
    void renderRoll(ssp::launchpad::Frame& f);
    void button(int x);
    void showCursor();

    GseqEngine& e_;
    mutable std::mutex lock_;
    int top_ = 0, left_ = 0;  // the window: rows top_ .. top_ + 5, steps left_ .. left_ + 7
    int pitchLow_ = 60;       // roll: the window's pitches, pitchLow_ .. pitchLow_ + 7; C4 .. G4 at first
    int base_ = START;
    bool follow_ = false;
    int lastKey_ = -1;  // the last key pad pressed: the default note in Step mode
    int cursorSeen_ = -1;
    // each key pad's held note, -1 while up, and its press's velocity: 0 on a model without velocity
    int held_[WINDOW_STEPS * KEY_ROWS] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int heldVelocity_[WINDOW_STEPS * KEY_ROWS] = {};
};

}  // namespace gseq
