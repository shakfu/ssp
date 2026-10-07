#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "engine/ReloadGate.h"
#include "engine/ScriptEngine.h"
#include "readerwriterqueue.h"

typedef struct CSOUND_ CSOUND;

namespace csnd {

// Runs a Csound orchestra (.csd). The orchestra keeps its own ksmps and channel counts; the host sets
// the sample rate. Controls arrive as channels "p1".."p8" (chnget), audio as inch 1..8 or ins, and
// MIDI through Csound's standard MIDI opcodes (massign, notnum, veloc, ...).
class CsoundEngine : public ssp::engine::ScriptEngine {
public:
    ~CsoundEngine() override;

    // audio thread; one k-cycle of latency
    void process(const float* const* in, float* const* out, int n) override;

    // Any one thread other than audio: a MIDI channel message. Dropped when 256 are queued.
    void midi(uint8_t status, uint8_t d1, uint8_t d2);

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;

private:
    struct Instance;
    struct Midi {
        uint8_t b[3];
    };
    static void destroy(Instance* i);
    static int32_t midiOpen(CSOUND*, void** user, const char*);
    static int32_t midiRead(CSOUND*, void* user, unsigned char* buf, int32_t nBytes);
    static int32_t midiClose(CSOUND*, void*);
    static void message(CSOUND*, int32_t attr, const char* format, va_list args);

    ssp::engine::ReloadGate<Instance> gate_;
    moodycamel::ReaderWriterQueue<Midi> midi_{ 256 };
};

}  // namespace csnd
