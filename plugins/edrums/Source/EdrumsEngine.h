#pragma once

#include <atomic>
#include <cstdint>

#include "Pattern.h"
#include "Voice.h"
#include "engine/Engine.h"

namespace edrums {

enum class Route { Stereo, Split, Random };

// Four Euclidean drum tracks, each a pattern driving a synthesized voice and a trigger output, stepped
// by a clock input. Each track steps `mult` times per clock pulse, or once every `div` pulses, over its
// own length, so the tracks can run in polymeter; a reset pulse realigns them.
class EdrumsEngine : public ssp::engine::Engine {
public:
    static constexpr int DRUMS = 4;
    enum { I_CLOCK, I_RESET, I_MAX };
    enum { O_L, O_R, O_1, O_2, O_3, O_4, O_TRIG_1, O_TRIG_2, O_TRIG_3, O_TRIG_4, O_MAX };
    static constexpr float TRIG_S = 0.005f;  // trigger pulse length, at most half the step spacing

    // Set from the audio thread before each process().
    struct Controls {
        Model model = Model::Kick;
        int hits = 0, steps = 16, rotate = 0;
        int mult = 1;         // steps per clock pulse, spread over the measured clock period
        int div = 1;          // clock pulses per step
        float chance = 1.0f;  // probability that an onset fires
        float swing = 0.5f;   // 0.5 straight .. 0.75: odd steps late by (swing - 0.5) x 2 steps
        float pitch = 0.5f, decay = 0.5f, level = 0.8f;
        float drive = 0.5f, sweep = 0.5f, tone = 0.5f, bright = 0.5f;  // 0.5 = the model as voiced
        bool mute = false;
        bool voice = true;     // off: the track only sends triggers
        bool trigger = false;  // a rising edge fires the drum at once
    };

    EdrumsEngine();

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;

    // audio thread
    Controls& controls(int d) { return ctl_[d]; }
    void setRoute(Route r) { route_ = r; }

    // any thread: what the display shows
    struct Info {
        uint16_t onsets = 0;  // bit s set if step s is an onset
        int steps = 16, position = 0;
        uint32_t hits = 0;  // count of voice triggers
    };
    Info info(int d) const;

private:
    struct Drum {
        Voice voice;
        Pattern pattern;
        Controls applied;
        bool fresh = true;  // nothing applied yet
        bool trigHigh = false;
        uint32_t rng = 0xC0FFEEu;
        uint32_t pulses = 0;   // clock pulses since reset, for div
        int subLeft = 0;       // multiplied steps still due in this clock period
        double subIn = 0.0;    // samples to the next one
        uint32_t count = 0;    // steps since reset; odd ones swing
        bool swung = false;    // a swung step is waiting
        double swingIn = 0.0;  // samples until it plays
        int trigLeft = 0;      // samples left of the trigger pulse
        bool trigGap = false;  // a hit while the trigger is high: one low sample first
        float pan = 0.5f;
        std::atomic<uint16_t> onsets{ 0 };
        std::atomic<int> steps{ 16 }, position{ 0 };
        std::atomic<uint32_t> hits{ 0 };
    };

    void apply(int d);
    void clock();
    void due(int d);
    void step(int d);
    double stepLength(int d) const;
    void reset();
    void fire(Drum& k, bool voice, int trigLen);
    int trigLen(int d) const;

    float sr_ = 48000.0f;
    Controls ctl_[DRUMS];
    Drum dk_[DRUMS];
    Route route_ = Route::Stereo;
    bool clockHigh_ = false, resetHigh_ = false;
    double sinceClock_ = 0.0, period_ = 0.0;  // samples since the last clock pulse, and between the last two
};

}  // namespace edrums
