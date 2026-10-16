#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "keys/KeyInput.h"

namespace ssp {

// Keys from KeyInput's thread, for the editor's timer on the message thread: modifier state, and the
// repeat of a held key, since KeyInput drops the keyboard's own repeats. No JUCE, so it tests natively.
class KeyQueue : public ssp::KeyTarget {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds DELAY{ 400 }, RATE{ 50 };  // repeat after DELAY, every RATE
    static constexpr size_t MAX = 256;  // queued keys kept while no editor takes them

    struct Key {
        uint8_t usage;
        uint8_t mods;  // TextBuffer::Mod
    };

    // any thread
    void keyDown(uint8_t usage) override;
    void keyUp(uint8_t usage) override;
    void keyTap(uint8_t usage, uint8_t mod) override;
    void releaseAll() override;

    // message thread: the keys pressed since the last call, then the repeats of a held key up to `now`
    std::vector<Key> take(Clock::time_point now);

private:
    uint8_t mods() const;  // from held_, lock_ held

    std::mutex lock_;
    std::deque<Key> keys_;
    uint8_t held_ = 0;  // the eight HID modifiers, bit n for usage 0xE0 + n
    uint8_t repeat_ = 0;  // the held key that repeats, 0 for none
    uint8_t repeatMods_ = 0;
    Clock::time_point next_;  // its next repeat
};

}  // namespace ssp
