#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "engine/Clock.h"
#include "engine/ScriptEngine.h"
#include "readerwriterqueue.h"

namespace alda {

// Plays an Alda score (.alda) in a loop. The first four parts, in the order the score declares them, are
// mono voices: voice k gates output k (5 V while a note sounds) and sets output 4 + k to the last note's
// pitch, 1 V/octave with MIDI 60 at 0 V. A rising edge on input 1 restarts the loop. A gate open when the
// loop restarts closes for GAP_MS, so the next note retriggers.
//
// The loop is the score rounded up to a 4/4 bar, or the length a comment gives, in quarter notes:
//
//   # @loop 6
//
// A clock comment makes the score follow a clock instead of its tempo: rising edges on input 2, 4 per
// quarter note, or N with `# @clock N`; or MIDI clock, 24 per quarter note, with Start, Stop and Continue,
// from the general panel's MIDI in with `# @clock midi`. Each pulse moves the score to the next pulse's
// position; between pulses it runs at the last interval's rate, never past the next pulse, so it stays
// locked and halts when the clock stops.
//
// The controls are fixed: p1 Transpose (semitones, CV and MIDI), p2 Tempo (a factor on the score's,
// free-running only), p3 Velocity (a factor on MIDI velocities), p4 Swing (every second 16th later, 50% to
// 75%), p5-p8 Mute 1-4 (a voice's gate and its part's MIDI notes). Their neutral settings are the defaults.
//
// A new score replaces the running one when the loop next ends, or at once while stopped. Stop (Ctrl-.)
// ends the score at the next block; a score with no notes stops too, so the editor's reset unloads.
//
// Every part also plays as MIDI, on the channels, programs, velocities, pan and volume Alda assigns: the
// audio thread queues the messages for midiOut(). Each restart releases the notes still sounding.
class AldaEngine : public ssp::engine::ScriptEngine {
public:
    static constexpr int VOICES = 4;
    static constexpr int TICKS_PER_QUARTER = 480;  // psnd's ALDA_TICKS_PER_QUARTER
    static constexpr int BAR = 4 * TICKS_PER_QUARTER;
    static constexpr int HELD = 8;  // notes a voice tracks at once
    static constexpr float GAP_MS = 1.0f;
    static constexpr int MIDI_QUEUE = 1024;  // messages; more in one stretch are dropped and counted

    AldaEngine();
    ~AldaEngine() override;

    void process(const float* const* in, float* const* out, int n) override;
    // Frees the score the audio thread replaced, then runs queued loads.
    void idle() override;
    std::string status() const override;
    std::string runState() const override;  // the status's first line: the bar and tempo, or "stopped"

    struct Event {
        enum Type : uint8_t { ON, OFF, TEMPO, PROGRAM, CC };
        static constexpr uint8_t NO_VOICE = 0xFF;  // a part past the fourth: MIDI only
        int32_t tick;
        Type type;
        uint8_t voice;    // ON and OFF: 0..VOICES-1, or NO_VOICE
        uint8_t channel;  // MIDI, 0..15
        int16_t value;    // ON and OFF: the note; TEMPO: bpm; PROGRAM: the program; CC: the controller
        uint8_t data2;    // ON: velocity; CC: the value
    };
    // A MIDI message, 2 or 3 bytes.
    struct Midi {
        uint8_t bytes[3];
        uint8_t size;
    };
    // One consumer thread, not audio: the next message the audio thread queued; false if none.
    bool midiOut(Midi& m) { return midi_.try_dequeue(m); }
    unsigned midiDropped() const { return midiDropped_.load(std::memory_order_relaxed); }

    // MIDI input thread: a clock tick at `seconds`, and the transport. Used while the clock is MIDI.
    void midiClock(double seconds) { clock_.midiClock(seconds); }
    void midiStart() { clock_.midiStart(); }
    void midiStop() { clock_.midiStop(); }
    void midiContinue() { clock_.midiContinue(); }
    enum Clock : uint8_t { FREE, INPUT, MIDI };  // as ssp::engine::Clock::Source
    static constexpr int CLOCK_INPUT = 1;  // input 2
    struct Score {
        Clock clock = FREE;
        int ppqn = 4;               // In 2's pulses per quarter note; MIDI clock has 24
        std::vector<Event> events;  // by tick; offs before ons at one tick
        int32_t length = BAR;       // ticks
        int bpm = 120;              // at tick 0
        std::string parts;          // the voices' part names, for status()
    };
    // Any thread but audio: `text` as a score, or false with `error` set. `path` names it in errors.
    static bool build(const std::string& text, const std::string& path, Score& score, std::string& error);

    bool canReset() const override { return true; }

    // MUTE + k: voice k's mute. CLOCK: 0 free, 1 In 2, 2 MIDI, 3 the score's `# @clock` line.
    enum Control { TRANSPOSE, TEMPO, VELOCITY, SWING, MUTE, CLOCK = MUTE + VOICES };
    static constexpr int CLOCK_SCORE = 3;
    static Specs controls();

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;
    bool dropAll(std::string& result) override;  // Ctrl-.: stop
    Specs declared(const std::string&) const override { return controls(); }

private:
    struct Voice {
        int8_t held[HELD];
        int count = 0;
        int pitch = 60;  // the CV's note; it holds after the gate closes
        int quiet = 0;   // samples the gate stays closed
    };
    void restart();  // audio thread: the loop's start, with a pending score if one waits
    void release();  // audio thread: note-off for every MIDI note sounding; every voice's gate closed
    void apply(const Event& e);
    void send(uint8_t status, uint8_t d1, uint8_t d2, uint8_t size = 3);  // audio thread
    void wrapPulse();  // audio thread: after a pulse that crossed the loop's end, its last events, then the loop

    // The worker hands scores to the audio thread through pending_, and the audio thread hands the one it
    // replaced back through retired_, so neither side frees on the audio thread. A swap waits while
    // retired_ is full.
    std::atomic<Score*> pending_{ nullptr };
    std::atomic<Score*> retired_{ nullptr };
    std::atomic<bool> stop_{ false };  // a stop for the audio thread; it waits while retired_ is full

    // audio thread
    Score* score_ = nullptr;
    ssp::engine::Clock clock_;  // its position: ticks into the loop
    size_t cursor_ = 0;
    Voice voices_[VOICES];
    uint32_t sounding_[16][4] = {};  // MIDI notes on, a bit per note and channel
    int8_t sent_[16][128];           // the note sent for each score note sounding, transposed; -1 none

    // the controls, read once a block
    int transpose_ = 0;
    double velocity_ = 1.0;
    bool mute_[VOICES] = {};

    moodycamel::ReaderWriterQueue<Midi> midi_{ MIDI_QUEUE };
    std::atomic<unsigned> midiDropped_{ 0 };

    // for status()
    std::atomic<double> posShown_{ 0.0 };
    std::atomic<int> lengthShown_{ 0 };  // 0 while nothing plays
    std::atomic<double> bpmShown_{ 0.0 };
    mutable std::mutex partsLock_;
    std::string parts_;  // the running score's, else the pending one's
};

static_assert(AldaEngine::TICKS_PER_QUARTER == ssp::engine::Clock::TICKS_PER_QUARTER, "one tick for score and clock");
static_assert(int(AldaEngine::FREE) == ssp::engine::Clock::FREE && int(AldaEngine::INPUT) == ssp::engine::Clock::INPUT &&
                      int(AldaEngine::MIDI) == ssp::engine::Clock::MIDI,
              "the score's clock is the Clock's source");

}  // namespace alda
