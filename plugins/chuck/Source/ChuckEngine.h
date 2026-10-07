#pragma once

#include <string>
#include <vector>

#include "engine/ReloadGate.h"
#include "engine/ScriptEngine.h"

class ChucK;

namespace ck {

// Runs a ChucK program (.ck) in one VM. Controls arrive as globals p1..p8 (`global float p1;`), audio
// as adc.chan(0..7) and dac.chan(0..7). MIDI is ChucK's own MidiIn, on the SSP's ALSA devices.
class ChuckEngine : public ssp::engine::ScriptEngine {
public:
    ~ChuckEngine() override;

    // Creates the VM on the first call and on a rate change, then (re)compiles the program.
    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    // Applies changed controls, which allocates in ChucK, then any queued load.
    void idle() override;

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;

private:
    ssp::engine::ReloadGate<ChucK> gate_;
    ChucK* vm_ = nullptr;
    float vmRate_ = 0.0f;
    std::vector<float> in_, out_;  // interleaved
    float applied_[PARAMS];
};

}  // namespace ck
