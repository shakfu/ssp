#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "ScWorld.h"
#include "ScsyDef.h"
#include "engine/ScriptEngine.h"

namespace scsy {

// Runs a SuperCollider SynthDef (.scsyndef) in its own scsynth World. Inputs 1..8 are audio buses
// 8..15 (`In.ar(NumOutputBuses.ir)`), outputs 1..8 buses 0..7. p1..p16 drive the def's named
// controls in order, through control buses. A sidecar beside the def, `foo.txt` for
// `foo.scsyndef`, can choose and scale them instead with csnd's lines:
//
//   // @p1 cutoff 20 20000 Hz log cv 3
//
// where the label names the control, and `cv 3` adds input 3 to it as CV (see ScriptEngine). A
// control with CV follows it at audio rate: it reads an audio bus the engine fills with the knob
// value moved by the CV, sample by sample, so a def that declares the control audio-rate gets every
// sample, and a control-rate one every 64.
// Without a sidecar, a control's range is 0 to max(1, 2 x default), or +-that for a negative default.
//
// MIDI: a def with a `gate` control is a voice. Each note starts a synth with `freq`, `velocity`
// (0..1) and `gate` 1, and its note off sets `gate` 0, so the def must free itself; at most
// ScWorld::VOICES notes are held. Notes set those three, so p1..p16 leave them out. A def without
// `gate` runs one synth, whose `freq` and `velocity` notes set when no control is mapped to them.
//
// A new synth starts with its def's defaults on its controls, as the knobs will show after a Load;
// a control read only at the start (an init-rate input, such as DC's) keeps that value. Its knobs
// drive it from the next block. The two synths of a reload use different control buses, so a
// failed load leaves the running synth's controls alone.
class ScsynthEngine : public ssp::engine::ScriptEngine {
public:
    // UGen plugins load from `ugenDir`, once per process (see ScWorld::open).
    explicit ScsynthEngine(std::string ugenDir): ugenDir_(std::move(ugenDir)) {}

    // Message thread, audio stopped: (re)opens the World at a new rate, then reloads the program.
    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    // Any thread but audio, such as MIDI's: velocity 1..127 starts a note, 0 ends it.
    void note(int note, int velocity);
    // UI thread: MIDI activity, once a note has arrived
    std::string status() const override;

    // The specs for `info`'s controls, from `sidecar` if not empty; false, with `error`, if it names
    // a control the def lacks. mapped[i] is the control p(i+1) drives.
    static bool controlSpecs(const SynthDefInfo& info, const std::string& sidecar, Specs& specs,
                             std::vector<ScWorld::Mapping>& mapped, std::string& error);

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    // Unused: compile() takes the built-in def's bytes from ScsyBuiltin.h when `path` is empty.
    const char* builtin() const override { return ""; }
    Specs declared(const std::string&) const override { return specs_; }
    bool readProgram(const std::string& path, std::string& bytes) const override;

private:
    // the range and CV input of each control, per bank of control buses
    struct Ranges {
        std::atomic<float> min[PARAMS], max[PARAMS];
        std::atomic<bool> log[PARAMS];
        std::atomic<int> cv[PARAMS];
        std::atomic<bool> audio[PARAMS];  // mapped to an audio-control bus
    };
    void run(const float* const* in, float* const* out, int n);

    const std::string ugenDir_;
    ScWorld world_;
    Ranges ranges_[2];
    std::atomic<int> bank_{ -1 };     // the running synth's controls; -1: none
    std::atomic<int> pending_{ -1 };  // a loading synth's, which read start_ until it runs
    std::atomic<float> start_[PARAMS];
    // the audio-control signals, one block each; sized in prepare()
    std::vector<float> signals_[ScWorld::AUDIO_CONTROLS];
    int capacity_ = 0;
    float rate_ = 0.0f;
    std::string openError_;
    bool pumping_ = false;  // in prepare(): no audio thread runs the engine
    Specs specs_;           // the last program compiled; compile() and declared() share a thread
};

}  // namespace scsy
