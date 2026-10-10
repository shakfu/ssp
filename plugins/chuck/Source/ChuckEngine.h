#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "engine/ReloadGate.h"
#include "engine/ScriptEngine.h"

class ChucK;

namespace ck {

// Runs a ChucK program (.ck) in one VM. Controls arrive as globals p1..p16 (`global float p1;`), audio
// as adc.chan(0..7) and dac.chan(0..7). MIDI notes from the general panel's input arrive as globals:
//
//   global int midiNotes[128];   // note << 8 | velocity, at index count % 128; velocity 0 ends it
//   global int midiCount;        // notes written so far
//   global Event midiEvent;      // broadcast after each
class ChuckEngine : public ssp::engine::ScriptEngine {
public:
    ~ChuckEngine() override;

    // Creates the VM on the first call and on a rate change, then (re)compiles the program.
    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    // Applies changed controls, which allocates in ChucK, then any queued load.
    void idle() override;
    // Any thread but audio, such as MIDI's: a note on (velocity 1..127) or off (0), to the globals.
    void midi(int note, int velocity);
    // UI thread: MIDI activity, once a note has arrived
    std::string status() const override;
    static constexpr int MIDI_NOTES = 128;  // midiNotes' size

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;

private:
    ssp::engine::ReloadGate<ChucK> gate_;
    ChucK* vm_ = nullptr;
    float vmRate_ = 0.0f;
    std::vector<float> in_, out_;  // interleaved
    float applied_[PARAMS];
    // ChucK's global requests are a ring with one writer: the worker and MIDI take turns, and a new VM
    // waits for both
    std::mutex globalsLock_;
    long midiCount_ = 0;
    std::atomic<unsigned> notes_{ 0 };
};

}  // namespace ck
