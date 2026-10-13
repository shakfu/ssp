// Replaces Atmel ASF's header of the same name, whose licence restricts redistribution to Atmel
// microcontroller products. Values from the USB HID specification: interface class 0x03, and the
// Keyboard/Keypad usage page (0x07) of the HID Usage Tables, under the names teletype uses.
#pragma once

#define HID_CLASS 0x03
#define HID_PROTOCOL_GENERIC 0x00

// boot report modifier byte
#define HID_MODIFIER_NONE 0x00
#define HID_MODIFIER_LEFT_CTRL 0x01
#define HID_MODIFIER_LEFT_SHIFT 0x02
#define HID_MODIFIER_LEFT_ALT 0x04
#define HID_MODIFIER_LEFT_UI 0x08
#define HID_MODIFIER_RIGHT_CTRL 0x10
#define HID_MODIFIER_RIGHT_SHIFT 0x20
#define HID_MODIFIER_RIGHT_ALT 0x40
#define HID_MODIFIER_RIGHT_UI 0x80

// usage page 0x07
#define HID_A 0x04
#define HID_B 0x05
#define HID_C 0x06
#define HID_D 0x07
#define HID_E 0x08
#define HID_F 0x09
#define HID_G 0x0A
#define HID_H 0x0B
#define HID_I 0x0C
#define HID_J 0x0D
#define HID_K 0x0E
#define HID_L 0x0F
#define HID_M 0x10
#define HID_N 0x11
#define HID_O 0x12
#define HID_P 0x13
#define HID_Q 0x14
#define HID_R 0x15
#define HID_S 0x16
#define HID_T 0x17
#define HID_U 0x18
#define HID_V 0x19
#define HID_W 0x1A
#define HID_X 0x1B
#define HID_Y 0x1C
#define HID_Z 0x1D
#define HID_1 0x1E
#define HID_2 0x1F
#define HID_3 0x20
#define HID_4 0x21
#define HID_5 0x22
#define HID_6 0x23
#define HID_7 0x24
#define HID_8 0x25
#define HID_9 0x26
#define HID_0 0x27
#define HID_ENTER 0x28
#define HID_ESCAPE 0x29
#define HID_BACKSPACE 0x2A
#define HID_TAB 0x2B
#define HID_SPACEBAR 0x2C
#define HID_UNDERSCORE 0x2D  // the - and _ key
#define HID_PLUS 0x2E        // the = and + key
#define HID_OPEN_BRACKET 0x2F
#define HID_CLOSE_BRACKET 0x30
#define HID_BACKSLASH 0x31
#define HID_COLON 0x33  // the ; and : key
#define HID_QUOTE 0x34
#define HID_TILDE 0x35  // the ` and ~ key
#define HID_COMMA 0x36
#define HID_DOT 0x37
#define HID_SLASH 0x38
#define HID_CAPS_LOCK 0x39
#define HID_F1 0x3A
#define HID_F2 0x3B
#define HID_F3 0x3C
#define HID_F4 0x3D
#define HID_F5 0x3E
#define HID_F6 0x3F
#define HID_F7 0x40
#define HID_F8 0x41
#define HID_F9 0x42
#define HID_F10 0x43
#define HID_F11 0x44
#define HID_F12 0x45
#define HID_PRINTSCREEN 0x46
#define HID_SCROLL_LOCK 0x47
#define HID_PAUSE 0x48
#define HID_INSERT 0x49
#define HID_HOME 0x4A
#define HID_PAGEUP 0x4B
#define HID_DELETE 0x4C
#define HID_END 0x4D
#define HID_PAGEDOWN 0x4E
#define HID_RIGHT 0x4F
#define HID_LEFT 0x50
#define HID_DOWN 0x51
#define HID_UP 0x52
#define HID_KEYPAD_NUM_LOCK 0x53
#define HID_KEYPAD_DIVIDE 0x54
#define HID_KEYPAD_MULTIPLY 0x55
#define HID_KEYPAD_MINUS 0x56
#define HID_KEYPAD_PLUS 0x57
#define HID_KEYPAD_ENTER 0x58
#define HID_KEYPAD_1 0x59
#define HID_KEYPAD_2 0x5A
#define HID_KEYPAD_3 0x5B
#define HID_KEYPAD_4 0x5C
#define HID_KEYPAD_5 0x5D
#define HID_KEYPAD_6 0x5E
#define HID_KEYPAD_7 0x5F
#define HID_KEYPAD_8 0x60
#define HID_KEYPAD_9 0x61
#define HID_KEYPAD_0 0x62
#define HID_KEYPAD_DOT 0x63
