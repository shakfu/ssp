#pragma once

// Beat length from MIDI clock, free of JUCE so it can be tested on the build machine.

namespace sfct {

class ClockTempo {
public:
    static constexpr unsigned PPQN = 24;  // MIDI clock ticks per beat
    // Averaging window. With +-1 ms of jitter per tick, 4 beats at 120 bpm measure within 0.1%,
    // under HYSTERESIS; a tempo change takes this many beats to register.
    static constexpr unsigned BEATS = 4;
    // A gap this long between ticks restarts the measurement: below 2.5 bpm, or the clock stopped.
    static constexpr double MAX_TICK_GAP = 1.0;
    // Changes smaller than this fraction are ignored, so clock jitter does not move loop lengths.
    static constexpr double HYSTERESIS = 0.002;

    // t: the tick's time in seconds
    void tick(double t) {
        if (count_ > 0 && (t - times_[(head_ + RING - 1) % RING] > MAX_TICK_GAP || t < times_[(head_ + RING - 1) % RING]))
            reset();
        times_[head_] = t;
        head_ = (head_ + 1) % RING;
        if (count_ < RING) count_++;
        if (count_ < RING) return;
        // the newest tick and the one BEATS before it: the ring holds BEATS * PPQN + 1 ticks
        double beat = (t - times_[head_]) / BEATS;
        if (beat <= 0.0) return;
        if (spb_ == 0.0 || beat > spb_ * (1.0 + HYSTERESIS) || beat < spb_ * (1.0 - HYSTERESIS)) spb_ = beat;
    }

    void reset() {
        count_ = 0;
        head_ = 0;
    }

    // seconds per beat; 0 until BEATS beats of ticks have arrived. Kept across a reset.
    double secondsPerBeat() const { return spb_; }
    double bpm() const { return spb_ > 0.0 ? 60.0 / spb_ : 0.0; }

private:
    static constexpr unsigned RING = BEATS * PPQN + 1;
    double times_[RING] = {};
    unsigned head_ = 0, count_ = 0;
    double spb_ = 0.0;
};

}  // namespace sfct
