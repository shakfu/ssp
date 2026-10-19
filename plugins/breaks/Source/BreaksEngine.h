#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "Storage.hpp"  // _core's rack/src: the card reader
#include "core_engine.h"
#include "engine/Engine.h"

namespace brks {

// The firmware's panel as the SSP's parameters set it. Knobs run 0..1, as the hardware's 0-1023.
struct Panel {
    enum { K_BREAK, K_GRIMOIRE, K_AMEN, K_RANDOM, K_SAMPLE, K_MAX };  // CoreControls' knob order
    enum { B_MODE, B_MULT, B_BANK, B_TAP, B_MAX };                    // and its button order
    float knobs[K_MAX] = { 0.0f, 0.0f, 0.0f, 0.5f, 0.0f };
    // The knobs' functions with Tap held. The firmware's own start (volume 234 of 244, with
    // saturation) stays until Volume changes.
    float volume = 0.5f, filter = 0.5f, gating = 0.0f;
    bool buttons[B_MAX] = {};  // Mode and Tap; Mult and Bank are taps from in 4 and 5
    uint8_t division = 2;       // Clock Out: 0 (8 per beat) to 6 (one per 2 bars), as Mult sets it
    // BPM, applied when it changes, as a Tap tempo: the card's tempo plays until then. A clock at In 1 overrides it.
    uint16_t tempo = 120;
    bool play = false;  // a press stops, or starts when stopped, as Tap + Mult does
};

// What the status panel shows, refreshed every 10 ms.
struct Status {
    bool loaded = false, stopped = false;
    unsigned bank = 0, slot = 0, slice = 0, rune = 0, tempo = 0, triggerMode = 0, division = 0;
    uint16_t effects = 0;  // bit per active effect, in sf->fx_active's order (FX_ in definitions.h)
    uint16_t allowed = 0;  // bit per effect the rune allows, in the Grimoire's order (effectN-on)
};

// The settings a preset keeps besides the parameters.
struct Saved {
    uint16_t tempo = 120;  // 0: the card's
    uint8_t bank = 0, triggerMode = 0;
};

// _core's breakbeat slicer firmware, from its desktop engine (core_engine.h), on the SSP. It runs at the
// firmware's 44.1 kHz in 441-frame blocks, resampled to the SSP's rate; clock and button inputs
// reach it at 44.1 kHz resolution. A card folder, as _core's sample manager exports it, loads on a
// worker; one bank's audio is held in memory.
class BreaksEngine : public ssp::engine::Engine {
public:
    // Inputs 1-8 triggers and CV from 9, as the SSP preset template. 7 and 8 are unused.
    enum { I_CLOCK, I_TAP, I_MODE, I_MULT, I_BANK, I_PLAY, I_SPARE7, I_SPARE8, I_AMEN, I_BREAK, I_SAMPLE, I_MAX };
    enum { O_L, O_R, O_TRIG, O_CLOCK, O_MAX };

    BreaksEngine();
    ~BreaksEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    void idle() override {
        if (onIdle) onIdle();
    }

    // Engine::idle's worker, every 10 ms: the processor's work there. Set before prepare(), cleared with idle stopped.
    std::function<void()> onIdle;

    // audio thread: set before each process()
    Panel panel;
    bool cvPatched[3] = {};  // Amen, Break, Sample: a patched CV replaces its knob, as on the hardware

    // Message thread: queues a load of the card folder at `path`, a bank folder in it, a file in one,
    // or a folder holding cards (the first, by name). bank: the bank to start in, -1 for the card's first.
    void load(const std::string& path, int bank = -1);
    // Message thread: the settings to apply once the next load is ready.
    void restore(const Saved& s);
    // Message thread: the next (1) or previous (-1) bank with samples, at the loop's end.
    void stepBank(int dir) { bankSteps_ += dir; }

    std::string folder() const;  // the card chosen, or empty
    std::string message() const;  // the loader's state, or why the path holds no card
    Status status() const;
    Saved saved() const;  // as of the last 0.5 s

    // The card folder `path` names, as load() resolves it, or empty.
    static std::string resolve(const std::string& path);

private:
    static bool read(void* user, unsigned bank, unsigned slot, unsigned variant, uint64_t offset, void* dst,
                     size_t bytes);
    void takeBank();
    void frame();  // one 44.1 kHz frame into hist_

    CoreEngine* core_ = nullptr;
    mutable ecto::Storage storage_;  // status() is not const
    ecto::Bank* active_ = nullptr;  // audio thread
    int requested_ = -1;

    // audio thread
    double step_ = 44100.0 / 48000.0, phase_ = 0.0;
    float hist_[4][2] = {};  // the last four frames, oldest first
    bool trig_ = false, clock_ = false, clockIn_ = false;
    bool gate_[6] = {};       // I_CLOCK..I_PLAY, after the Schmitt trigger
    std::atomic<int> bankSteps_{ 0 };
    unsigned pressFor_[Panel::B_MAX] = {};  // frames a press is held at least
    bool playWas_ = false;
    float volume_, filter_, gating_;  // as last applied
    uint8_t division_ = 0xff;
    uint16_t tempo_;
    unsigned frames_ = 0;
    CoreControls controls_{};

    mutable std::mutex lock_;  // the fields below; the audio thread only tries it
    std::string folder_, error_;
    Status status_;
    Saved saved_;

    ecto::Queue<Saved, 4> restores_;  // message thread to audio thread
};

}  // namespace brks
