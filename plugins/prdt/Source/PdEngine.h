#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "engine/ReloadGate.h"
#include "engine/ScriptEngine.h"
#include "readerwriterqueue.h"

namespace prdt {

// Runs a Pure Data patch (.pd) in its own libpd instance. Inputs 1..8 reach [adc~ 1]..[adc~ 8] and
// [dac~ 1]..[dac~ 8] reach outputs 1..8, one 64-frame tick late. A comment in the patch declares a
// control in ScriptEngine's syntax:
//
//   @p1 cutoff 20 20000 Hz log cv 3
//
// and its value goes to [r cutoff] after a load and whenever it changes; an undeclared control goes
// to [r p1]..[r p16]. MIDI channel messages reach [notein], [ctlin], [bendin] and the others.
// Abstractions resolve from the patch's folder, then from `searchPath`. A load fails if any object
// cannot be created, and leaves the running patch alone. See docs/dev/libpd.md.
class PdEngine : public ssp::engine::ScriptEngine {
public:
    explicit PdEngine(std::vector<std::string> searchPath = {}): searchPath_(std::move(searchPath)) {}
    ~PdEngine() override;

    // Audio thread. Outputs silence for a block in which Pd's class lock is taken, as when another
    // module's load creates an instance.
    void process(const float* const* in, float* const* out, int n) override;

    // Any one thread other than audio: a MIDI channel message. Dropped when 256 are queued.
    void midi(uint8_t status, uint8_t d1, uint8_t d2);
    // UI thread: MIDI activity, once a note has arrived, then the console's last lines
    std::string status() const override;

    // The @pN lines in a patch's comments (#X text records)
    static Specs patchSpecs(const std::string& text);

    static constexpr int TICK = 64;          // Pd's block
    static constexpr size_t CONSOLE = 4;     // console lines in status()

protected:
    bool compile(const std::string& text, const std::string& path, std::string& error) override;
    const char* builtin() const override;
    Specs declared(const std::string& text) const override { return patchSpecs(text); }

private:
    struct Instance;
    struct Midi {
        uint8_t b[3];
    };
    struct Line {
        char text[120];
    };
    static void destroy(Instance* i);
    static void print(const char* s);
    void addConsole(const std::string& line) const;

    const std::vector<std::string> searchPath_;
    ssp::engine::ReloadGate<Instance> gate_;
    moodycamel::ReaderWriterQueue<Midi> midi_{ 256 };
    std::atomic<unsigned> notes_{ 0 };
    // printed on the audio thread; status() moves them to console_
    mutable moodycamel::ReaderWriterQueue<Line> printed_{ 64 };
    mutable std::mutex consoleLock_;
    mutable std::deque<std::string> console_;
};

}  // namespace prdt
