#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace edrums {

// Euclidean pattern: `onsets` hits spread as evenly as possible over `steps` (1..16), rotated.
// From sk-engines dsp/cpattern (MIT, Synthux Academy), trimmed to what edrums uses.
class Pattern {
public:
    static constexpr int SIZE = 16;

    Pattern() { build(); }

    void setLength(int steps) {
        steps = std::clamp(steps, 1, SIZE);
        if (steps == steps_) return;
        steps_ = steps;
        onsets_ = std::min(onsets_, steps_);
        rotation_ %= steps_;
        if (next_ >= steps_) next_ = 0;
        build();
    }
    void setOnsets(int onsets) {
        onsets = std::clamp(onsets, 0, steps_);
        if (onsets == onsets_) return;
        onsets_ = onsets;
        build();
    }
    void setRotation(int steps) { rotation_ = ((steps % steps_) + steps_) % steps_; }

    int steps() const { return steps_; }
    int position() const { return next_; }  // the step trigger() reads next
    void reset() { next_ = 0; }

    // Advances one step; true if the step it leaves is an onset.
    bool trigger() {
        bool on = isOnset(next_);
        if (++next_ >= steps_) next_ = 0;
        return on;
    }

    // the rotated pattern, as trigger() plays it
    bool isOnset(int step) const {
        int point = step % steps_ - rotation_;
        if (point < 0) point += steps_;
        return pattern_[size_t(point)] != 0;
    }

private:
    // Christoffel-word algorithm ("Creating Rhythms", Hollos & Hollos)
    void build() {
        const int n = steps_;
        pattern_.fill(0);
        if (onsets_ == 0) return;
        if (onsets_ >= n) {
            for (int i = 0; i < n; i++) pattern_[size_t(i)] = 1;
            return;
        }
        int y = onsets_, a = y, x = n - onsets_, b = x, i = 0;
        pattern_[size_t(i++)] = 1;
        while (a != b) {
            if (a > b) {
                pattern_[size_t(i)] = 1;
                b += x;
            } else {
                pattern_[size_t(i)] = 0;
                a += y;
            }
            i++;
        }
        pattern_[size_t(i++)] = 0;
        // not coprime: only part of the slots were filled, so repeat the cell
        for (int j = 0; i + j < n; j++) pattern_[size_t(i + j)] = pattern_[size_t(j)];
    }

    std::array<uint8_t, SIZE> pattern_{};
    int steps_ = SIZE, onsets_ = 0, rotation_ = 0, next_ = 0;
};

}  // namespace edrums
