#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Stretcher.h"
#include "engine/Engine.h"
#include "engine/ReloadGate.h"
#include "engine/Station.h"
#include "engine/Stream.h"

namespace pstretch {

enum class Route { Stereo, Split, Random };
enum class Source { Live, Capture, File };
enum class ModShape { Sine, Triangle, Follow };
enum class ModTarget { Diffusion, Stretch, Tone };

// Two real-time PaulStretch decks. Each deck stretches its input, a capture of its recent input, or
// a clip streamed from a folder of files. The FFT work of each hop is spread over the blocks the hop
// lasts, so no block carries a whole transform.
class PstretchEngine : public ssp::engine::Engine {
public:
    static constexpr int DECKS = 2;
    static constexpr int WINDOWS[] = { 4096, 8192, 16384, 32768 };
    // inputs, per deck from in[deck * I_PER_DECK]; then the crossfade CV
    enum { I_IN, I_PITCH, I_STRETCH, I_MIX, I_GATE, I_PER_DECK };
    enum { I_XFADE = DECKS * I_PER_DECK, I_MAX };
    enum { O_L, O_R, O_A, O_B, O_LFO_A, O_LFO_B, O_GATE_A, O_GATE_B, O_MAX };

    // Set from the audio thread before each process().
    struct Controls {
        float stretch = 0.5f;   // 0..1 maps 1x..64x
        float diffuse = 1.0f;   // 0 clean resynthesis .. 1 random phase
        float pitch = 0.5f;     // +/-1 octave
        float tone = 1.0f;      // one-pole low-pass, 0 dark .. 1 open
        float mix = 1.0f;       // dry/wet
        Source source = Source::Live;
        float clip = 0.0f;      // 0..1 across the folder
        float position = 0.0f;  // 0..1 of the clip, where it opens
        float modRate = 0.3f;   // 0..1 maps 0.03..7.7 Hz
        float modDepth = 0.0f;
        ModShape modShape = ModShape::Sine;
        ModTarget modTarget = ModTarget::Diffusion;
        bool freeze = false;  // a rising edge toggles freeze
        bool grab = false;    // a rising edge re-captures, in Capture
    };

    PstretchEngine();
    ~PstretchEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    void idle() override;

    // audio thread
    Controls& controls(int d) { return ctl_[d]; }
    void setCrossfade(float x) { xfade_ = x; }
    void setRoute(Route r);
    void setWindow(int n) { wantWindow_.store(n, std::memory_order_relaxed); }

    // message thread: the folder of clips
    void setRoot(const std::string& dir);
    std::string root() const;

    struct Info {
        bool frozen = false;
        int clip = -1, clips = 0;
        std::string clipName;
        int window = 0;
    };
    Info info(int d) const;

private:
    // The FFT tables and both voices for one window size; rebuilt by the worker when it changes.
    struct Core;

    struct Link {                                    // audio <-> worker, per deck
        std::atomic<ssp::engine::Stream*> next{ nullptr };
        std::atomic<ssp::engine::Stream*> retired{ nullptr };
        std::atomic<int> clip{ -1 };                 // the clip to open, audio -> worker
        std::atomic<float> position{ 0.0f };
        std::atomic<uint32_t> request{ 0 };          // bumped after clip and position are set
        std::atomic<int> open{ -1 };                 // the clip the worker opened
        std::atomic<bool> frozen{ false };
    };

    struct AudioDeck {
        ssp::engine::Stream* stream = nullptr;
        bool rewind = false;  // a new stream: restart the voice's file heads
        bool frozen = false, freezeHigh = false, grabHigh = false, gateHigh = false;
        int clip = -1;              // clip requested
        float position = 0.0f;      // position requested
        int pendingClip = -1;       // a change waiting to settle
        float pendingPos = 0.0f;
        int pendingAge = -1;        // samples it has held; -1 for none
        uint32_t gen = 0;
        float lfoPhase = 0.0f, follow = 0.0f, lp = 0.0f, lfo = 0.5f;
        float tone = 1.0f;  // the low-pass coefficient this block, after modulation
        int gateOut = 0;  // samples left of the gate-out pulse
        float panL = 0.7071f, panR = 0.7071f;
    };

    float modulate(int d, const float* const* in, int n, Stretcher& v);
    void feedFile(int d, Stretcher& v);
    void requestFile(int d, int n);
    void openClip(int d, int clip, float position);
    Core* build(int window);

    float sr_ = 48000.0f;
    int maxBlock_ = 1;
    Controls ctl_[DECKS];
    Link sh_[DECKS];
    AudioDeck au_[DECKS];
    ssp::engine::ReloadGate<Core> gate_;
    Core* applied_ = nullptr;  // the core the voices were last set up in, audio thread
    std::vector<float> wet_[DECKS], file_;
    float xfade_ = 0.5f;
    Route route_ = Route::Stereo;
    uint32_t rng_ = 0x9e3779b9u;
    std::atomic<int> wantWindow_{ 8192 };
    std::atomic<int> window_{ 0 };  // the core's window, for display
    std::atomic<int> clips_{ 0 };
    std::atomic<uint32_t> gen_{ 0 };  // bumped by each scan, worker -> audio

    // worker
    std::vector<ssp::engine::Station> stations_;
    uint32_t served_[DECKS] = {};
    ssp::engine::Stream* live_[DECKS] = {};
    std::atomic<bool> rootChanged_{ true };

    mutable std::mutex lock_;  // root_ and the names below; never taken by the audio thread
    std::string root_;
    std::vector<std::string> names_;
};

}  // namespace pstretch
