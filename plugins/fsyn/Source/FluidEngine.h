#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "engine/Engine.h"
#include "engine/ReloadGate.h"
#include "readerwriterqueue.h"

namespace fsyn {

// A MIDI channel message: status and two data bytes.
struct Event {
    uint8_t status = 0, d1 = 0, d2 = 0;
};

struct Settings {
    float gain = 0.5f;    // FluidSynth's synth.gain, 0..2
    float reverb = 0.7f;  // reverb send level, 0..1
    float chorus = 0.5f;  // chorus level, 0..1 for FluidSynth's 0..4
    int voices = 64;      // polyphony, 1..MAX_VOICES
};

// FluidSynth playing one SF2 soundfont. A load builds a new synth on the worker thread and swaps it in,
// so a large font neither stalls the audio thread nor the MIDI it receives meanwhile. The channels'
// programs, banks, controllers and bends carry over to the new synth; notes playing end.
class FluidEngine : public ssp::engine::Engine {
public:
    static constexpr int MAX_VOICES = 256;  // allocated at load: a lower Voices setting does not allocate
    enum { I_GAIN, I_MAX };                 // CV, read once per block: added to Gain, 1.0 (5 V) per unit
    enum { O_L, O_R, O_MAX };

    FluidEngine();
    ~FluidEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    void idle() override;

    // audio thread: set before each process()
    Settings settings;

    // MIDI thread, the only producer. Applied at the next block; false when the queue is full.
    bool post(const Event& e) {
        if (events_.try_enqueue(e)) return true;
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Message thread: queues a load of the soundfont at `path`; empty for none, which is silent.
    // A font that fails to load leaves the one playing.
    void load(const std::string& path);
    std::string path() const;   // the font chosen, loaded or queued
    std::string font() const;   // the font playing, empty for none
    std::string error() const;  // the last load's error, or empty
    bool loading() const;

    // any thread
    int voicesPlaying() const { return playing_.load(std::memory_order_relaxed); }
    int eventsDropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    struct Synth;
    void track(const Event& e);  // the channel state an event sets
    void send(Synth& s, const Event& e);
    void replay(Synth& s);  // the channel state to a synth just swapped in
    void apply(Synth& s, float cvGain);

    ssp::engine::ReloadGate<Synth> synth_;
    std::atomic<unsigned> synthGen_{ 0 };

    mutable std::mutex lock_;  // the strings and pending_
    std::string path_, font_, error_;
    bool pending_ = false, busy_ = false;
    std::atomic<float> rate_{ 0.0f };

    moodycamel::ReaderWriterQueue<Event> events_{ 1024 };

    // audio thread: the channel state, so a swap keeps it; -1 where no message set it
    int16_t cc_[16][128];
    int16_t program_[16], bend_[16];
    unsigned seenGen_ = 0;
    Settings applied_;
    float gainPlayed_ = -1.0f;

    std::atomic<int> playing_{ 0 };
    std::atomic<int> dropped_{ 0 };
};

}  // namespace fsyn
