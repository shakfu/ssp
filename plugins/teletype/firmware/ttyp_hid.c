// A raw HID boot keyboard report into the firmware, as its USB host driver delivers one. The
// firmware's own HID timer then reads it, so key repeat and modifier handling stay teletype's.
// monome-rack's hardware_hidMessage skips that path: it calls process_keypress directly.
#include <stdint.h>

extern void hid_parse_frame(uint8_t* data, uint8_t size);

__attribute__((visibility("default"))) void hardware_hidFrame(const uint8_t* report, uint8_t size) {
    uint8_t frame[8] = { 0 };
    for (uint8_t i = 0; i < size && i < 8; i++) frame[i] = report[i];
    hid_parse_frame(frame, 8);
}
