#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

#include "engine/Clock.h"
#include "engine/Engine.h"
#include "readerwriterqueue.h"

namespace gseq {

// A grid of ROWS rows of up to STEPS steps; docs/dev/gseq.md has the design. Every row's step lasts
// 1 / division quarter notes whatever its length, so rows of different lengths drift (polymeter). A step
// plays a note on the row's MIDI channel, on its CV voice, or both: voice k gates output k (5 V), sets
// output 4 + k to the pitch (1 V/octave, MIDI 60 at 0 V) and output 8 + k to the velocity (127 at 5 V)
// when the row's velocity CV is on: held until the next note, or with velocityGated, only while the gate is
// open (0 V otherwise, also in a retrigger's gap). A note that a step starts while the row's last still sounds
// retriggers: the gate closes for GAP_MS, and MIDI gets a note-off first. A gate stays open MIN_GATE_MS.
//
// Inputs: 1 resets to the first step, 2 is the clock for the INPUT source (ssp::engine::Clock).
//
// A new row plays MIDI on its own channel; rows 1-4 also play CV voices 1-4, with velocity CV.
//
// Recording (plugins/gseq/TODO.md has the decisions): notes from noteIn() become steps. Rec arms on the
// cursor's row, follows the cursor until the take starts, and records the row's next whole pass, each note
// at the nearest step, replacing the row's steps, then stops. Step entry writes each note at the cursor,
// which moves on when every key is up. A chord's notes, lowest first, go to the cursor's row and the rows
// below it, wrapping to row 1; only the cursor's row is replaced. A note's duration is how long it was
// held, snapped to DURATION_EIGHTHS. While either is on, notes also sound through each row's own routing
// (echo). Notes from playIn() only sound, from the cursor's row down, in any mode.
//
// Roll mode (docs/dev/gseq-roll.md): the rows are one track's voices. addNote() places a note at a step and
// pitch into a row by the doc's rules; recording and step entry place notes the same way, a Rec take
// replaces every row, and keys echo on a row free to sound them. A mono track keeps every note on row 1, one
// per step. Playback is the same in both modes.
//
// The pattern is a word per step and per row, set and read from any thread: edits need no handoff, and
// the audio thread reads them as it plays.
class GseqEngine : public ssp::engine::Engine {
public:
    static constexpr int ROWS = 8, STEPS = 32, VOICES = 4;
    static constexpr int RESET_INPUT = 0, CLOCK_INPUT = 1;
    static constexpr int DIVISIONS = 6;
    static constexpr int DIVISION[DIVISIONS] = { 1, 2, 3, 4, 6, 8 };  // steps a quarter note
    // a step's duration, in eighths of a step: 1/8 .. 7/8, then 1, 1.5, 2, 3, 4, 6, 8, 12, 16 steps
    static constexpr int DURATIONS = 16;
    static constexpr int DURATION_EIGHTHS[DURATIONS] = { 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 24, 32, 48, 64, 96, 128 };
    static constexpr float GAP_MS = 1.0f, MIN_GATE_MS = 2.0f;
    static constexpr int MIDI_QUEUE = 1024;  // messages; more in one stretch are dropped and counted

    struct Step {
        bool on = false;
        uint8_t note = 60, velocity = 100;  // 0-127, 1-127
        uint8_t duration = 3;               // DURATION_EIGHTHS index; 3 is half a step
        static Step unpack(uint32_t w);     // clamped, so any word is a valid step
        uint32_t pack() const;
    };
    struct Row {
        uint8_t length = 16;    // 1-STEPS
        uint8_t division = 3;   // DIVISION index; 3 is 16ths
        uint8_t channel = 0;    // MIDI, 1-16; 0 off
        uint8_t voice = 0;      // CV, 1-VOICES; 0 off
        bool velocityCv = false, mute = false;
        bool velocityGated = false;  // velocity CV only while the gate is open
        bool mono = false;           // roll, row 1's: one note per step, all on row 1
        static Row unpack(uint32_t w);
        uint32_t pack() const;
    };

    GseqEngine();

    // any thread
    void setStep(int row, int step, Step s) { steps_[row][step].store(s.pack(), std::memory_order_relaxed); }
    Step step(int row, int step) const { return Step::unpack(steps_[row][step].load(std::memory_order_relaxed)); }
    // A voice another row holds is refused: the row keeps its own. True if the row took `r` as given.
    bool setRow(int row, Row r);
    Row row(int row) const { return Row::unpack(rows_[row].load(std::memory_order_relaxed)); }
    int playing(int row) const { return playing_[row].load(std::memory_order_relaxed); }  // the step; -1 before
    double bpm() const { return bpmShown_.load(std::memory_order_relaxed); }
    unsigned midiDropped() const { return midiDropped_.load(std::memory_order_relaxed); }

    // UI thread: Rec, pressed: arms, or stops the take armed or recording. The row is the cursor's.
    void toggleRecord() { recordToggle_.store(true, std::memory_order_release); }
    enum Record : uint8_t { IDLE, ARMED, RECORDING };
    Record record() const { return Record(recordState_.load(std::memory_order_relaxed)); }
    int recordRow() const { return recordRow_.load(std::memory_order_relaxed); }
    // UI thread: step entry on or off; on stops a take
    void setStepEntry(bool on) { stepEntry_.store(on, std::memory_order_relaxed); }
    bool stepEntry() const { return stepEntry_.load(std::memory_order_relaxed); }
    // any thread: the cursor, the row Rec arms and where step entry writes; step entry moves it on
    void setCursor(int row, int col) { cursor_.store(row << 8 | col, std::memory_order_relaxed); }
    int cursorRow() const { return cursor_.load(std::memory_order_relaxed) >> 8; }
    int cursorCol() const { return cursor_.load(std::memory_order_relaxed) & 0xFF; }
    // any thread: roll mode's cursor pitch; the cursor's column is its step
    void setCursorPitch(int p) { cursorPitch_.store(p, std::memory_order_relaxed); }
    int cursorPitch() const { return cursorPitch_.load(std::memory_order_relaxed); }
    // MIDI input thread: a note from a keyboard; velocity 0 is its note-off. Ignored unless recording.
    void noteIn(int note, int velocity);
    // MIDI input thread, as noteIn(): a note that sounds from the cursor's row down and writes no step
    void playIn(int note, int velocity);
    // any thread: the last note noteIn() or playIn() started, in any mode; -1 before one
    int lastNote() const { return lastNote_.load(std::memory_order_relaxed); }

    // Any thread: roll mode on or off. Switching it on gives every row row 1's MIDI channel, length,
    // division and velocity CV; rows 1-4 voices 1-4 if row 1 has a voice, else none; mutes cleared.
    void setRoll(bool on);
    bool roll() const { return roll_.load(std::memory_order_relaxed); }
    // Roll's mono track (row 1's `mono`), for one oscillator: every note on row 1, one per step, a new note
    // replacing the step's. Switching it on keeps one note per step, row 1's or the lowest row's, and
    // clears the rest. Any thread.
    void setMono(bool on);
    bool mono() const { return roll() && row(0).mono; }
    // Roll, any thread. The row holding pitch p at step s, or -1.
    int findNote(int s, int p) const;
    // Sets the note at step s, of pitch st.note, or adds it to a row by the placement rules. Its row, or -1
    // when every row sounds at s.
    int addNote(int s, Step st);
    int removeNote(int s, int p);  // its row, or -1
    // A note's duration; it moves to a row free for its whole length if it would now overlap the next note
    // in its own, and one is free. Its row, or -1 if there is no such note.
    int setNoteDuration(int s, int p, uint8_t duration);

    // audio thread, before process(): the clock's source, its input's pulses a quarter, tempo and swing
    void setClock(ssp::engine::Clock::Source s, int ppqn) { source_ = s, clock_.setPpqn(ppqn); }
    void setBpm(double bpm) { clock_.setBpm(bpm); }
    void setSwing(double swing) { clock_.setSwing(swing); }  // 0 straight .. 0.5

    // MIDI input thread
    void midiClock(double seconds) { clock_.midiClock(seconds); }
    void midiStart() { clock_.midiStart(); }
    void midiStop() { clock_.midiStop(); }
    void midiContinue() { clock_.midiContinue(); }

    struct Midi {
        uint8_t bytes[3];
        uint8_t size;
    };
    // One consumer thread, not audio: the next message the audio thread queued; false if none.
    bool midiOut(Midi& m) { return midi_.try_dequeue(m); }

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;

private:
    // a row's note as it sounds, audio thread
    struct Playing {
        int64_t lastStep = std::numeric_limits<int64_t>::min();  // steps since 0; the sentinel replays the current
        bool sounding = false;
        double end = 0.0;  // the swung position the note ends at
        int age = 0;       // samples since it started
        int minAge = 0;    // samples before it may end: MIN_GATE_MS, after a retrigger's gap
        int channel = 0, note = 0, voice = 0;  // as it started: an edit to the row does not orphan it
    };
    // a key held while recording or in step entry
    struct Held {
        bool active = false;
        uint8_t note = 0, velocity = 0;
        bool written = false;     // a step holds it: row and col
        int row = 0, col = 0;
        int64_t chord = 0;        // its step, real time; its notes share it. Step entry: -1; playIn(): PLAY
        int echo = -1;            // the row sounding it
        int64_t since = 0;        // the sample it started
    };
    static constexpr int HELD = 16;
    static constexpr int64_t PLAY = -2;
    struct NoteIn {
        uint8_t note, velocity;
        bool play;  // playIn()
    };
    void takeNotes();  // block start: Rec and step entry, then the notes queued
    void keyDown(const NoteIn& k, double at);
    void keyUp(int note);
    void place(int64_t chord, int base);  // a chord's notes, lowest first, from row `base` down
    void placeRoll(Held& h);              // roll: a recorded or step-entered note, by the placement rules
    // Roll placement (docs/dev/gseq-roll.md): rule 1, a row sounding pitch p at s; 2, a row free at s for the
    // whole duration; 3, a row free at s. fits() tests one row against one rule.
    bool fits(int r, int s, int p, uint8_t duration, int rule) const;
    int placeRow(int s, int p, uint8_t duration, bool strict) const;  // strict: rules 1 and 2 only
    int echoRow(int pitch) const;  // roll, audio thread: the row sounding pitch, else the first silent one
    uint8_t durationIndex(int64_t samples, const Row& row) const;
    void startRecording(double at);
    bool takeStep(int r, int64_t k, int s);  // the take's row reaches step k (s in the row): true if written

    void start(int r, const Row& row, const Step& s, double at);
    void stop(int r);
    void stopAll(bool replay);  // replay: each row's step at the position plays again
    void send(uint8_t status, uint8_t d1, uint8_t d2);

    std::atomic<uint32_t> steps_[ROWS][STEPS];
    std::atomic<uint32_t> rows_[ROWS];
    std::atomic<int> playing_[ROWS];

    // audio thread
    ssp::engine::Clock clock_;
    ssp::engine::Clock::Source source_ = ssp::engine::Clock::FREE;
    float sampleRate_ = 48000.0f;
    int gap_ = 48, minGate_ = 96;  // samples
    Playing notes_[ROWS];
    // each voice's outputs: gate open, samples it stays closed first (retrigger), pitch and velocity
    bool gate_[VOICES] = {};
    int closed_[VOICES] = {};
    float pitch_[VOICES] = {}, velocity_[VOICES] = {};
    bool velocityGated_[VOICES] = {};  // as the row that last started the voice

    // recording: requests from the UI and notes from the MIDI thread, state for the UI
    std::atomic<bool> recordToggle_{ false }, stepEntry_{ false };
    std::atomic<int> cursor_{ 0 }, cursorPitch_{ 60 }, lastNote_{ -1 };
    std::atomic<bool> roll_{ false };
    std::atomic<uint8_t> recordState_{ IDLE };
    std::atomic<int> recordRow_{ 0 };
    moodycamel::ReaderWriterQueue<NoteIn> notesIn_{ 256 };
    // audio thread
    int64_t take_ = 0;         // the take's first step, as steps since 0 on its row
    int takeLength_ = 0;       // its row's length when armed: the steps the take covers
    uint32_t written_[ROWS] = {};  // per row, the take's steps a note was written to: kept, not played this pass
    Held held_[HELD];
    int64_t samples_ = 0;      // processed since start

    moodycamel::ReaderWriterQueue<Midi> midi_{ MIDI_QUEUE };
    std::atomic<unsigned> midiDropped_{ 0 };
    std::atomic<double> bpmShown_{ 0.0 };
};

}  // namespace gseq
