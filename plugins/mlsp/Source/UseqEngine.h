#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "engine/Clock.h"
#include "engine/ScriptEngine.h"

namespace sig {
struct SignalEngine;
}

namespace mlsp {

// Runs ModuLisp, the language of the uSEQ module, on uSEQ's signal engine. Each output is a function of
// time, sampled every TICK frames. Out 1-4 are d1-d4, gates: 5 V while above 0.5. Out 5-8 are a1-a4, 0..1
// as 0..5 V. The program reads in1-in8 (1 above 1 V, else 0), ain1-ain8 (0..5 V as 0..1) and p1-p16, the
// controls in the ranges their `; @pN` lines declare.
//
// t is seconds since the start, at the program's tempo. A clock line makes it follow a clock instead, so
// that `beat` moves one per quarter note of the clock:
//
//   ; @clock [N]    rising edges on In 2, N per quarter note (default 4)
//   ; @clock midi   MIDI clock, with Start, Stop and Continue
//
// `; @reset` makes a rising edge on In 1 set t to 0.
//
// A program runs only if all of it compiles on a fresh engine. It then replaces the running one on the live
// engine, so outputs both assign keep their state (uSEQ's live edit). Ctrl-E runs forms on the live engine
// beside the program, as uSEQ's editor sends them; one that fails leaves the forms before it applied, as
// on the module. Ctrl-. clears every output.
class UseqEngine : public ssp::engine::ScriptEngine {
public:
    static constexpr int TICK = 16;  // frames per engine tick: 3 kHz at 48 kHz
    static constexpr int GATES = 4, CVS = 4;
    static constexpr int RESET_INPUT = 0, CLOCK_INPUT = 1;
    static constexpr float GATE_IN = 0.2f;  // 1 V
    static constexpr size_t MAX_FORM = 65535;  // bytes: uSEQ's token spans are 16-bit

    UseqEngine();
    ~UseqEngine() override;

    void process(const float* const* in, float* const* out, int n) override;
    std::string status() const override;
    std::string runState() const override;  // the bar and tempo, or "paused"
    bool canReset() const override { return true; }

    // MIDI input thread: a clock tick at `seconds`, and the transport. Used with `; @clock midi`.
    void midiClock(double seconds) { clock_.midiClock(seconds); }
    void midiStart() { clock_.midiStart(); }
    void midiStop() { clock_.midiStop(); }
    void midiContinue() { clock_.midiContinue(); }

    // A top-level form of a program: its bytes and its first line, 1-based.
    struct Form {
        size_t offset, length;
        int line;
    };
    // Splits `text` into top-level forms, skipping comments; false with `error` set for an unbalanced
    // bracket, an unclosed string or a form over MAX_FORM. `line` is the number of the text's first line.
    static bool split(const std::string& text, int line, std::vector<Form>& forms, std::string& error);

    // The program's clock and reset lines.
    struct Options {
        ssp::engine::Clock::Source clock = ssp::engine::Clock::FREE;
        int ppqn = 4;
        bool reset = false;
    };
    static Options options(const std::string& text);

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;
    bool runPart(const std::string& part, const std::string& before, const std::string& path,
                 std::string& result) override;
    bool dropAll(std::string& result) override;

private:
    // Evaluates `forms` of `text` on `engine` in order; false with `error` at the first that fails, else the
    // last form's value in `result`. The caller holds evalLock().
    bool eval(sig::SignalEngine& engine, const std::string& text, const std::vector<Form>& forms,
              std::string& error, std::string* result);
    void showOutputs();       // the live engine's assigned outputs into outputs_; liveLock_ held
    void tick(const float* const* in, int i);  // audio thread, liveLock_ held
    void restartTime();       // audio thread: t to 0

    // uSEQ's symbol table and diagnostic buffers are process-wide, so every instance evaluates under one lock.
    static std::mutex& evalLock();

    std::unique_ptr<sig::SignalEngine> live_;
    std::mutex liveLock_;  // live_; the audio thread tries it and holds its outputs for a block it misses

    std::atomic<uint8_t> clockSource_{ ssp::engine::Clock::FREE };
    std::atomic<int> ppqn_{ 4 };
    std::atomic<bool> reset_{ false };

    // audio thread
    ssp::engine::Clock clock_;
    std::vector<double> cells_, inputs_, values_, workspace_;  // the executor's arrays
    double t_ = 0.0, dt_ = 0.0;  // seconds; dt_ since the last tick that ran
    double lastPos_ = 0.0;       // clock_'s position at the last sample
    double bpm_ = 120.0, beatsPerBar_ = 4.0, offset_ = 0.0;
    bool playing_ = true;
    int countdown_ = 0;  // frames to the next tick

    // for status()
    std::atomic<double> tShown_{ 0.0 }, bpmShown_{ 120.0 }, clockBpmShown_{ 120.0 }, barShown_{ 4.0 };
    std::atomic<bool> playingShown_{ true };
    mutable std::mutex shownLock_;
    std::string outputs_;  // the assigned outputs, "d1 d2 a1"
};

}  // namespace mlsp
