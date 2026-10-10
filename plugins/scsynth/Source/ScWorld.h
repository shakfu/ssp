#pragma once

#include <atomic>
#include <mutex>
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
    static constexpr int VOICES = 16;       // held notes; another releases the oldest
    static constexpr int VOICE_NODES = 32;  // voice synths, held or releasing; another frees the oldest

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
    // nextBank(): a control bus (setControl), or an audio bus (process's `controls`). With `voices`,
    // the def is a voice: it starts no synth, and each noteOn() starts one. With `pump`, for a caller
    // whose audio is stopped, this thread runs the engine while it waits; audio-mapped controls then
    // read setPumpControl()'s values.
    bool loadDef(const std::string& bytes, std::string& error, const std::vector<Mapping>& mapped = {},
                 bool voices = false, bool pump = false, int timeoutMs = 5000);
    // Worker thread: sets a control of the running synth.
    void set(const std::string& control, float value);

    // Any thread but audio, such as MIDI's; performed at the next block. For a voice def, starts a
    // synth with `freq`, `velocity` (0..1) and `gate` 1, mapped as the load mapped the running
    // synth's controls. Otherwise sets the running synth's `freq` and `velocity`.
    void noteOn(int note, float velocity);
    // As noteOn(): sets `gate` 0 on the note's voices, which free themselves.
    void noteOff(int note);
    // Any thread: note ons received since open(), and notes held now (a voice def only).
    unsigned notesReceived() const { return notes_.load(std::memory_order_relaxed); }
    int notesHeld() const { return held_.load(std::memory_order_relaxed); }

    // The bank of control buses the next load maps to. Each load has its own, so it can set the new
    // synth's start values without touching the running one's. Worker thread.
    int nextBank() const { return bank_ == 0 ? 1 : 0; }
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
    // /n_map or /n_mapa for `node`: the mapped controls of that kind, on `bank`'s buses
    std::string mapMessage(const char* address, int node, const std::vector<Mapping>& mapped, int bank,
                           bool audio) const;
    static constexpr int VOICE_GROUP = 900;
    static constexpr int VOICE_FIRST = 2000, VOICE_IDS = 10000;  // voice node ids
    int audioControlBus(int k) const;
    // Worker: reads replies until one from `address` whose first argument is `arg`, or the timeout.
    // Collects the /fail replies it reads.
    bool waitFor(const std::string& address, const std::string& arg, int timeoutMs,
                 std::vector<std::string>& failures);

    World* world_ = nullptr;
    Reply replies_[REPLIES];
    std::atomic<unsigned> replyWrite_{ 0 };
    bool pump_ = false;
    int bank_ = -1;    // the running def's bank of control buses
    int node_ = 0;     // the running synth; 0: none, or a voice def
    std::string def_;  // the running SynthDef's name in this World

    // notes, against loads and close; guards world_ for them, and the members below
    struct Voice {
        int node = 0;  // 0: free
        int note = -1;
        bool held = false;
    };
    void release(Voice& v);
    std::mutex noteLock_;
    bool voices_ = false;
    std::vector<Mapping> mapped_;
    Voice voice_[VOICE_NODES];
    int voiceNext_ = 0;  // the oldest voice, which the next note replaces
    void countHeld();
    std::atomic<unsigned> notes_{ 0 };
    std::atomic<int> held_{ 0 };
    unsigned voiceIds_ = 0;

    // the FIFO for blocks that are not multiples of 64: inputs, then audio controls
    float fifoIn_[CHANNELS + AUDIO_CONTROLS][BLOCK] = {};
    float fifoOut_[CHANNELS][BLOCK] = {};
    int fill_ = 0;
    // what the audio-control buses hold while pumping
    float pumpControls_[AUDIO_CONTROLS][BLOCK] = {};
    const float* pumpPtrs_[AUDIO_CONTROLS];
};

}  // namespace scsy
