#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "Station.h"
#include "Stream.h"
#include "engine/Engine.h"

namespace radio {

enum class Route { Stereo, Split, Random };

// Two independent virtual RadioMusic decks over one library of banks. Every station plays on a
// free-running clock, so tuning to it lands where it would be had it played all along. File I/O runs
// in idle(); the audio thread only pulls from the open Stream.
class RadioEngine : public ssp::engine::Engine {
public:
    static constexpr int DECKS = 2;
    // CV inputs, per deck, from in[deck * I_PER_DECK]
    enum { I_STATION, I_START, I_SPEED, I_RESET, I_PER_DECK };
    enum { O_L, O_R, O_A, O_B, O_MAX };

    // Set from the audio thread before each process().
    struct Controls {
        float station = 0.0f;  // 0..1 across the bank
        float start = 0.0f;    // 0..1 of the station, applied on a switch or reset
        float speed = 0.0f;    // octaves
        float noise = 0.0f;    // static between stations, 0..1
        float level = 1.0f;
        float bank = 0.0f;  // 0..1 across the banks
        bool reset = false;  // a rising edge resets, as the reset input does
    };

    RadioEngine();
    ~RadioEngine() override;

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;
    void idle() override;

    // audio thread
    Controls& controls(int d) { return ctl_[d]; }
    void setCrossfade(float x);
    void setRoute(Route r);
    void setRawRate(float hz) { rawRate_.store(hz, std::memory_order_relaxed); }
    // fade between the old and new stream on every switch or reset
    void setFade(float ms) { fadeMs_ = ms; }
    // move Start to jump at once, rather than on the next switch or reset
    void setStartImmediate(bool pot, bool cv) {
        startPotImmediate_ = pot;
        startCvImmediate_ = cv;
    }

    // message thread
    void setRoot(const std::string& dir);
    std::string root() const;

    struct Info {
        int bank = -1, banks = 0, station = -1, stations = 0;
        std::string bankName, stationName;
        bool playing = false, error = false;
    };
    Info info(int d) const;

private:
    // Station choice in the audio thread is tagged with the worker's bank-scan generation, so a
    // choice made against a previous scan is ignored.
    static uint32_t pack(uint32_t gen, int station) { return gen << 16 | uint32_t(station + 1); }

    struct Shared {  // audio <-> worker
        std::atomic<Stream*> next{ nullptr };     // published by the worker, taken by audio
        std::atomic<Stream*> retired{ nullptr };  // handed back by audio for the worker to free
        std::atomic<int> bank{ -1 };              // wanted bank, audio -> worker
        std::atomic<uint32_t> gen{ 0 };           // scan generation, worker -> audio
        std::atomic<int> stations{ 0 };
        std::atomic<int> open{ -1 };              // station the worker last opened
        std::atomic<uint32_t> commit{ 0 };        // packed station choice, audio -> worker
        std::atomic<float> start{ 0.0f };
        std::atomic<bool> reset{ false };
        std::atomic<bool> playing{ false };
        std::atomic<bool> error{ false };
    };

    // a stream and its varispeed read position
    struct Voice {
        Stream* s = nullptr;
        float phase = 0.0f, a = 0.0f, b = 0.0f;
        bool primed = false;
        bool playing() const { return s != nullptr && s->playing(); }
    };

    struct AudioDeck {
        Voice cur, prev;    // prev fades out over fadeMs_ as cur fades in
        float fade = 1.0f;  // 0..1 through the fade
        float staticEnv = 0.0f, noiseLp = 0.0f;
        float startKnob = -1.0f, startCv = 0.0f;  // last values, to detect a move
        uint32_t pending = 0;
        int64_t pendingAge = 0;  // samples the pending choice has held
        bool resetHigh = false;
        float panL = 0.7071f, panR = 0.7071f;
    };

    struct WorkerDeck {
        int bank = -1;
        std::vector<Station> stations;
        uint32_t gen = 0;
        int open = -1;
        Stream* live = nullptr;  // the newest published stream
        int64_t rescanAt = 0;    // ms; retries an empty bank
    };

    int quantise(int d, float x, int n) const;
    void render(int d, float* mono, int n, float octaves);
    float read(Voice& v, float step);
    float noise(int d);
    void publish(int d, Stream* s);
    void openStation(int d, int station, bool live);
    void scanBank(int d, int bank);

    float sr_ = 48000.0f;
    float noiseK_ = 0.2f;  // static one-pole coefficient
    std::vector<float> mono_[DECKS];
    Controls ctl_[DECKS];
    Shared sh_[DECKS];
    AudioDeck au_[DECKS];
    WorkerDeck wk_[DECKS];

    std::atomic<uint64_t> clock_{ 0 };  // output frames since start
    std::atomic<float> rawRate_{ 44100.0f };
    std::atomic<int> banks_{ 0 };
    float gA_ = 1.0f, gB_ = 1.0f;
    float fadeMs_ = 15.0f;
    bool startPotImmediate_ = false, startCvImmediate_ = false;
    Route route_ = Route::Stereo;
    uint32_t rng_ = 0x9e3779b9u;

    // worker
    std::vector<std::string> bankDirs_;
    std::atomic<bool> rootChanged_{ true };

    mutable std::mutex lock_;  // root_ and the names below; never taken by the audio thread
    std::string root_;
    std::string bankName_[DECKS], stationName_[DECKS];
};

}  // namespace radio
