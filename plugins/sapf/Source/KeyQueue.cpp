#include "KeyQueue.h"

#include "TextBuffer.h"

namespace sapf {

namespace {

// a HID modifier byte (bit 0 left Ctrl, 1 left Shift, 2 left Alt, 3 left GUI, 4-7 the right ones) to
// TextBuffer's modifiers
uint8_t fromHid(uint8_t bits) {
    uint8_t m = 0;
    if (bits & 0x11) m |= TextBuffer::CTRL;
    if (bits & 0x22) m |= TextBuffer::SHIFT;
    if (bits & 0x44) m |= TextBuffer::ALT;
    return m;
}

bool modifier(uint8_t usage) {
    return usage >= 0xE0 && usage <= 0xE7;
}

}  // namespace

uint8_t KeyQueue::mods() const {
    return fromHid(held_);
}

void KeyQueue::keyDown(uint8_t usage) {
    std::lock_guard<std::mutex> lock(lock_);
    if (modifier(usage)) {
        held_ |= uint8_t(1 << (usage - 0xE0));
        return;
    }
    if (keys_.size() < MAX) keys_.push_back({ usage, mods() });
    repeat_ = usage;
    repeatMods_ = mods();
    next_ = Clock::now() + DELAY;
}

void KeyQueue::keyUp(uint8_t usage) {
    std::lock_guard<std::mutex> lock(lock_);
    if (modifier(usage)) held_ &= uint8_t(~(1 << (usage - 0xE0)));
    else if (usage == repeat_) repeat_ = 0;
}

void KeyQueue::keyTap(uint8_t usage, uint8_t mod) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!modifier(usage) && keys_.size() < MAX) keys_.push_back({ usage, fromHid(mod) });
}

void KeyQueue::releaseAll() {
    std::lock_guard<std::mutex> lock(lock_);
    held_ = 0;
    repeat_ = 0;
}

std::vector<KeyQueue::Key> KeyQueue::take(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(lock_);
    std::vector<Key> out(keys_.begin(), keys_.end());
    keys_.clear();
    // actions repeat only as plain keys: a held Ctrl-Enter evaluates once
    if (repeat_ && !(repeatMods_ & TextBuffer::CTRL)) {
        if (now - next_ > std::chrono::seconds(1)) next_ = now;  // after a stall, no burst
        for (; next_ <= now && out.size() < MAX; next_ += RATE) out.push_back({ repeat_, repeatMods_ });
    }
    return out;
}

}  // namespace sapf
