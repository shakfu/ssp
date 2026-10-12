#pragma once

#include <cstdint>

#ifdef __linux__
#include <linux/input-event-codes.h>

namespace ttyp {

// Linux evdev key code to HID keyboard usage (USB HID Usage Tables, page 0x07); 0 for keys
// teletype has no use for.
inline uint8_t hidUsage(unsigned code) {
    switch (code) {
        case KEY_A: return 0x04; case KEY_B: return 0x05; case KEY_C: return 0x06; case KEY_D: return 0x07;
        case KEY_E: return 0x08; case KEY_F: return 0x09; case KEY_G: return 0x0A; case KEY_H: return 0x0B;
        case KEY_I: return 0x0C; case KEY_J: return 0x0D; case KEY_K: return 0x0E; case KEY_L: return 0x0F;
        case KEY_M: return 0x10; case KEY_N: return 0x11; case KEY_O: return 0x12; case KEY_P: return 0x13;
        case KEY_Q: return 0x14; case KEY_R: return 0x15; case KEY_S: return 0x16; case KEY_T: return 0x17;
        case KEY_U: return 0x18; case KEY_V: return 0x19; case KEY_W: return 0x1A; case KEY_X: return 0x1B;
        case KEY_Y: return 0x1C; case KEY_Z: return 0x1D;
        case KEY_1: return 0x1E; case KEY_2: return 0x1F; case KEY_3: return 0x20; case KEY_4: return 0x21;
        case KEY_5: return 0x22; case KEY_6: return 0x23; case KEY_7: return 0x24; case KEY_8: return 0x25;
        case KEY_9: return 0x26; case KEY_0: return 0x27;
        case KEY_ENTER: return 0x28; case KEY_ESC: return 0x29; case KEY_BACKSPACE: return 0x2A;
        case KEY_TAB: return 0x2B; case KEY_SPACE: return 0x2C; case KEY_MINUS: return 0x2D;
        case KEY_EQUAL: return 0x2E; case KEY_LEFTBRACE: return 0x2F; case KEY_RIGHTBRACE: return 0x30;
        case KEY_BACKSLASH: return 0x31; case KEY_SEMICOLON: return 0x33; case KEY_APOSTROPHE: return 0x34;
        case KEY_GRAVE: return 0x35; case KEY_COMMA: return 0x36; case KEY_DOT: return 0x37;
        case KEY_SLASH: return 0x38; case KEY_CAPSLOCK: return 0x39;
        case KEY_F1: return 0x3A; case KEY_F2: return 0x3B; case KEY_F3: return 0x3C; case KEY_F4: return 0x3D;
        case KEY_F5: return 0x3E; case KEY_F6: return 0x3F; case KEY_F7: return 0x40; case KEY_F8: return 0x41;
        case KEY_F9: return 0x42; case KEY_F10: return 0x43; case KEY_F11: return 0x44; case KEY_F12: return 0x45;
        case KEY_SYSRQ: return 0x46; case KEY_SCROLLLOCK: return 0x47; case KEY_PAUSE: return 0x48;
        case KEY_INSERT: return 0x49; case KEY_HOME: return 0x4A; case KEY_PAGEUP: return 0x4B;
        case KEY_DELETE: return 0x4C; case KEY_END: return 0x4D; case KEY_PAGEDOWN: return 0x4E;
        case KEY_RIGHT: return 0x4F; case KEY_LEFT: return 0x50; case KEY_DOWN: return 0x51; case KEY_UP: return 0x52;
        case KEY_NUMLOCK: return 0x53; case KEY_KPSLASH: return 0x54; case KEY_KPASTERISK: return 0x55;
        case KEY_KPMINUS: return 0x56; case KEY_KPPLUS: return 0x57; case KEY_KPENTER: return 0x58;
        case KEY_KP1: return 0x59; case KEY_KP2: return 0x5A; case KEY_KP3: return 0x5B; case KEY_KP4: return 0x5C;
        case KEY_KP5: return 0x5D; case KEY_KP6: return 0x5E; case KEY_KP7: return 0x5F; case KEY_KP8: return 0x60;
        case KEY_KP9: return 0x61; case KEY_KP0: return 0x62; case KEY_KPDOT: return 0x63;
        case KEY_102ND: return 0x64;
        case KEY_LEFTCTRL: return 0xE0; case KEY_LEFTSHIFT: return 0xE1; case KEY_LEFTALT: return 0xE2;
        case KEY_LEFTMETA: return 0xE3; case KEY_RIGHTCTRL: return 0xE4; case KEY_RIGHTSHIFT: return 0xE5;
        case KEY_RIGHTALT: return 0xE6; case KEY_RIGHTMETA: return 0xE7;
        default: return 0;
    }
}

}  // namespace ttyp
#endif
