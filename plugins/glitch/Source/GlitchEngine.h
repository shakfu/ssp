// SPDX-License-Identifier: GPL-3.0-only
//
// GPLv3 as a combined work with GlitchVoice.h; see ../NOTICE.md.
#pragma once

#include <atomic>

#include "GlitchVoice.h"
#include "engine/Engine.h"

namespace glitch {

enum class Route { Stereo, Split, Random };

// Two independent lo-fi noise voices, each running one of the 12 Noisferatu algorithms.
class GlitchEngine : public ssp::engine::Engine {
public:
    static constexpr int DECKS = 2;
    // CV inputs, per deck, from in[deck * I_PER_DECK]
    enum { I_PITCH, I_P1, I_P2, I_PER_DECK };
    enum { O_L, O_R, O_A, O_B, O_MAX };

    // Set from the audio thread before each process().
    struct Controls {
        int algo = 0;
        float p1 = 0.5f, p2 = 0.5f;  // the algorithm's two parameters
        float pitch = 0.5f;          // +/-2 octaves around the centre
        float tone = 1.0f;           // one-pole low-pass, 0 dark .. 1 open
        float level = 0.8f;
        bool regen = false;  // a rising edge refills the glitch buffer
    };

    void prepare(float sampleRate, int maxBlock) override;
    void process(const float* const* in, float* const* out, int n) override;

    // audio thread
    Controls& controls(int d) { return ctl_[d]; }
    void setCrossfade(float x);
    void setRoute(Route r);

    // any thread: the algorithm each deck runs
    int algo(int d) const { return shown_[d].load(std::memory_order_relaxed); }

private:
    struct Deck {
        Voice voice;
        int algo = -1;  // applied to the voice; -1 before the first block
        float p1 = -1.0f, p2 = -1.0f, pitch = -1.0f;
        float lp = 0.0f;
        bool regenHigh = false;
        float panL = 0.7071f, panR = 0.7071f;
    };

    void apply(int d, const float* const* cv, int n);

    Controls ctl_[DECKS];
    Deck dk_[DECKS];
    std::atomic<int> shown_[DECKS] = { { 0 }, { 0 } };
    float gA_ = 1.0f, gB_ = 1.0f;
    Route route_ = Route::Stereo;
    uint32_t rng_ = 0x9e3779b9u;
};

}  // namespace glitch
