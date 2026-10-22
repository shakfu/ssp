#include "Clock.h"

#include <algorithm>
#include <cmath>

namespace ssp::engine {

void Clock::midiClock(double seconds) {
    double gap = seconds - lastMidiTick_;
    midiInterval_.store(lastMidiTick_ >= 0.0 && gap > 0.0 && gap < 1.0 ? gap : 0.0, std::memory_order_relaxed);
    lastMidiTick_ = seconds;
    midiPulses_.fetch_add(1, std::memory_order_release);
}

void Clock::follow(Source source, float sampleRate) {
    sampleRate_ = sampleRate;
    perSample_ = double(TICKS_PER_QUARTER) / 60.0 / double(sampleRate);
    if (source == source_) return;
    source_ = source;
    if (source != FREE) lastGrid_ = std::floor(pos_ / tpp()) * tpp();  // pulses stay on the grid
    rate_ = 0.0;
    halted_ = false;
}

Clock::Transport Clock::transport() {
    // the ticks this block takes; later ones wait, so a stream of ticks cannot hold the block
    pulsesDue_ = midiPulses_.load(std::memory_order_acquire);
    if (source_ != MIDI) return NONE;
    const auto t = Transport(transport_.exchange(NONE, std::memory_order_acq_rel));
    switch (t) {
    case START:
        rewind();
        halted_ = false;
        break;
    case STOP: halted_ = true; break;
    case CONTINUE: halted_ = false; break;
    default: break;
    }
    return t;
}

bool Clock::midiPulse() {
    while (pulsesSeen_ != pulsesDue_) {
        pulsesSeen_++;
        if (source_ != MIDI || halted_) continue;
        pulse(midiInterval_.load(std::memory_order_relaxed) * sampleRate_);
        return true;
    }
    return false;
}

void Clock::pulse(double interval) {
    if (interval > 0.0) rate_ = tpp() / interval;
    pos_ = lastGrid_ = lastGrid_ + tpp();
}

bool Clock::resetInput(float v) {
    if (!resetHigh_ && v > TRIG_HIGH) {
        resetHigh_ = true;
        rewind();
        return true;
    }
    if (resetHigh_ && v < TRIG_LOW) resetHigh_ = false;
    return false;
}

bool Clock::clockInput(float v) {
    bool pulsed = false;
    if (!clockHigh_ && v > TRIG_HIGH) {
        clockHigh_ = true;
        pulsed = source_ == INPUT;
        if (pulsed) pulse(sinceEdge_);
        sinceEdge_ = 0.0;
    } else if (clockHigh_ && v < TRIG_LOW) {
        clockHigh_ = false;
    }
    if (sinceEdge_ > 0.0 || clockHigh_) sinceEdge_ = std::min(sinceEdge_ + 1.0, 1e9);
    return pulsed;
}

bool Clock::advance() {
    if (source_ == MIDI && halted_) return false;
    if (source_ == FREE) {
        pos_ += bpm_ * tempo_ * perSample_;
    } else {
        double limit = lastGrid_ + tpp();
        pos_ = std::min(pos_ + (rate_ > 0.0 ? rate_ : bpm_ * perSample_), std::nextafter(limit, 0.0));
    }
    return true;
}

double Clock::swung() const {
    if (swing_ <= 0.0) return pos_;
    constexpr double SIXTEENTH = TICKS_PER_QUARTER / 4.0;
    double base = std::floor(pos_ / (2 * SIXTEENTH)) * 2 * SIXTEENTH, off = pos_ - base;
    double first = SIXTEENTH * (1.0 + swing_);  // the first 16th of each pair lasts longer
    return base + (off < first ? off / (1.0 + swing_) : SIXTEENTH + (off - first) / (1.0 - swing_));
}

double Clock::bpm() const {
    return source_ != FREE && rate_ > 0.0 ? rate_ / perSample_ : bpm_ * tempo_;
}

}  // namespace ssp::engine
