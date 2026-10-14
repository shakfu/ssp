// KeyInput in a library of its own, as each plugin .so has it; keys_test.cpp loads two copies.
#include "keys/KeyInput.h"

namespace {
struct Target : ssp::KeyTarget {
    void keyDown(uint8_t) override {}
    void keyUp(uint8_t) override {}
    void keyTap(uint8_t, uint8_t) override {}
    void releaseAll() override {}
} target;
}  // namespace

extern "C" {
void keys_acquire(uint16_t port) {
    ssp::KeyInput::setKeyboardScan(false);  // never the desktop's keyboard
    ssp::KeyInput::acquire(port, "KEYS_TEST_KEYBOARD");
}
void keys_show() { ssp::KeyInput::setTarget(&target); }
bool keys_wants() { return ssp::KeyInput::wantsGrab(); }
void keys_release() {
    ssp::KeyInput::clearTarget(&target);
    ssp::KeyInput::release();
}
}
