#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "Cartridge.h"
#include "engine/Engine.h"
#include "engine/ReloadGate.h"
#include "readerwriterqueue.h"

class Dexed;

namespace dexed {

// A MIDI channel message: status (channel ignored) and two data bytes.
struct Event {
    uint8_t status = 0, d1 = 0, d2 = 0;
};

// Performance controls. Voice, transpose and the offsets apply over the cartridge's voice, so a
// program change keeps them.
struct Settings {
    int voice = 0;           // 0..31 in the cartridge
    int voices = 16;         // polyphony, 1..MAX_VOICES
    float level = 1.0f;      // output gain
    int transpose = 0;       // semitones, -24..24
    float cutoff = 1.0f;     // 0..1
    float resonance = 0.0f;  // 0..1
    int algorithm = 0;       // 0: the voice's; 1..32 replaces it
    int feedback = 0;        // added to the voice's 0..7
    int opLevel[6] = {};     // added to operators 1..6's output level, 0..99
    int lfoRate = 0;         // added to the voice's LFO speed, 0..99
    int lfoDepth = 0;        // added to the voice's LFO pitch depth, 0..99
    int bendRange = 2;       // semitones, 0..12
    int portamento = 0;      // 0 off, 1..99
    bool mono = false;
    int engine = 1;          // Synth_Dexed's MSFA, MKI, OPL
};

// Program change 0..31 selects that voice; others return -1.
inline int programVoice(int program) {
    return program >= 0 && program < Cartridge::VOICES ? program : -1;
}

// base with the settings' offsets applied: what the synth plays.
void applySettings(const uint8_t* base, const Settings& s, uint8_t* out);

// Synth_Dexed with a FIFO, so any block size works, and voice reaping after every 64-sample chunk.
class DexedEngine : public ssp::engine::Engine {
public:
    static constexpr int MAX_VOICES = 16;
    static constexpr int CHUNK = 64;  // Synth_Dexed renders whole chunks of _N_ samples
    enum { I_CUTOFF, I_MOD, I_MAX };  // CV, read once per block: Cutoff adds; Mod is the wheel, if above it
    enum { O_L, O_R, O_MAX };

    DexedEngine();
    ~DexedEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;

    // audio thread: set before each process()
    Settings settings;

    // MIDI thread, the only producer. Applied at the next block; false when the queue is full.
    bool post(const Event& e) {
        if (events_.try_enqueue(e)) return true;
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Message thread: the cartridge the voice setting selects from; null for the init voice.
    // Waits at most one block for the audio thread to let go of the old one.
    void setCartridge(std::unique_ptr<Cartridge> c);

    // any thread
    int notesPlaying() const { return playing_.load(std::memory_order_relaxed); }
    int eventsDropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    class Synth;

    void apply(float cvCutoff);
    void applyWheel(float cvMod);
    void handle(const Event& e);
    void releaseAll();

    std::unique_ptr<Synth> synth_;
    float sampleRate_ = 0.0f;

    moodycamel::ReaderWriterQueue<Event> events_{ 512 };
    ssp::engine::ReloadGate<Cartridge> cart_;
    std::atomic<unsigned> cartGen_{ 0 };

    // audio thread
    unsigned seenGen_ = ~0u;
    int seenVoice_ = -1;
    Settings applied_;
    bool force_ = true;  // apply every setting at the next block
    uint8_t base_[Cartridge::VOICE_BYTES] = {};
    uint8_t played_[Cartridge::VOICE_BYTES] = {};
    uint8_t held_[128] = {};  // pitch sent for each key, plus 1; 0 when up
    float fifo_[CHUNK] = {};
    int fifoPos_ = CHUNK;  // CHUNK: empty
    float cutoffPlayed_ = -1.0f;
    int wheelMidi_ = 0, wheelPlayed_ = -1;

    std::atomic<int> playing_{ 0 };
    std::atomic<int> dropped_{ 0 };
};

}  // namespace dexed
