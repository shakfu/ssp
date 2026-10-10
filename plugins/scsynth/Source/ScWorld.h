#pragma once

#include <atomic>
#include <string>
#include <vector>

struct World;
struct ReplyAddress;

namespace scsy {

// One scsynth World, run by the host's audio callback instead of an audio device. Inputs 1..8 are
// audio buses 8..15 and outputs 1..8 are buses 0..7, as on a stock scsynth with 8 in and 8 out.
// The header keeps SC's headers out of the code that includes it.
class ScWorld {
public:
    static constexpr int CHANNELS = 8;
    static constexpr int BLOCK = 64;     // scsynth's block; the host's need not be a multiple
    static constexpr int CONTROLS = 16;  // control buses per bank; the host writes them
    // Audio buses for controls that follow a signal: CONTROLS per bank, bank-major, the World's last.
    static constexpr int AUDIO_CONTROLS = 2 * CONTROLS;

    // A control loadDef() maps: to control bus i of its bank, or with `audio` to audio-control bus i.
    struct Mapping {
        std::string name;
        bool audio = false;
    };

    ScWorld();
    ~ScWorld() { close(); }
    ScWorld(const ScWorld&) = delete;
    ScWorld& operator=(const ScWorld&) = delete;

    // Message thread, audio stopped. UGens load from `pluginDir` (and its subdirectories) once per
    // process, by the first World opened; later Worlds share that set and ignore the argument.
    bool open(float sampleRate, const std::string& pluginDir, std::string& error);
    // Message thread, audio stopped.
    void close();
    bool isOpen() const { return world_ != nullptr; }

    // Worker thread: loads the first SynthDef in a .scsyndef and starts it in place of the running
    // synth. On failure the running synth is left alone and `error` says why.
    bool load(const std::string& path, std::string& error, int timeoutMs = 5000);
    // As load(), from the file's bytes. mapped[i] names the control that reads bus i of bank
    // nextBank(): a control bus (setControl), or an audio bus (process's `controls`). With `pump`,
    // for a caller whose audio is stopped, this thread runs the engine while it waits; audio-mapped
    // controls then read setPumpControl()'s values.
    bool loadDef(const std::string& bytes, std::string& error, const std::vector<Mapping>& mapped = {},
                 bool pump = false, int timeoutMs = 5000);
    // Worker thread: sets a control of the running synth.
    void set(const std::string& control, float value);

    // The bank of control buses the next load maps to. Each synth has its own, so a load can set the
    // new synth's start values without touching the running one's. Worker thread.
    int nextBank() const { return node_ == 1000 ? 1 : 0; }
    // Audio thread, before process(), or any thread while audio is stopped: the value a mapped
    // control reads from bus i of `bank`.
    void setControl(int bank, int i, float value);
    // Audio stopped: the value audio-control bus i of `bank` holds while loadDef() pumps.
    void setPumpControl(int bank, int i, float value);

    // Audio thread. A block of a multiple of 64 frames, with none buffered, runs without latency;
    // otherwise frames pass through a 64-frame FIFO, which adds 64 frames of latency. `controls`, if
    // given, holds AUDIO_CONTROLS signals of n frames (bank * CONTROLS + i) for the audio-control
    // buses; a null signal leaves its bus alone.
    void process(const float* const* in, float* const* out, int n, const float* const* controls = nullptr);

private:
    // replies from scsynth's audio and NRT threads, read by the worker; a full slot drops the reply
    struct Reply {
        std::atomic<bool> full{ false };
        int size = 0;
        char data[256];
    };
    static constexpr int REPLIES = 16;
    static void onReply(ReplyAddress* addr, char* buf, int size);
    bool send(const std::string& packet);
    int controlBus(int bank, int i) const;
    int audioControlBus(int k) const;
    // Worker: reads replies until one from `address` whose first argument is `arg`, or the timeout.
    // Collects the /fail replies it reads.
    bool waitFor(const std::string& address, const std::string& arg, int timeoutMs,
                 std::vector<std::string>& failures);

    World* world_ = nullptr;
    Reply replies_[REPLIES];
    std::atomic<unsigned> replyWrite_{ 0 };
    bool pump_ = false;
    int node_ = 0;     // the running synth; 0: none
    std::string def_;  // its SynthDef's name in this World

    // the FIFO for blocks that are not multiples of 64: inputs, then audio controls
    float fifoIn_[CHANNELS + AUDIO_CONTROLS][BLOCK] = {};
    float fifoOut_[CHANNELS][BLOCK] = {};
    int fill_ = 0;
    // what the audio-control buses hold while pumping
    float pumpControls_[AUDIO_CONTROLS][BLOCK] = {};
    const float* pumpPtrs_[AUDIO_CONTROLS];
};

}  // namespace scsy
