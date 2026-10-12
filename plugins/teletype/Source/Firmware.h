#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ttyp {

// teletype's src/serializer.h, for scene text
struct SceneWriter {
    void (*write_buffer)(void* self, uint8_t* buffer, uint16_t size);
    void (*write_char)(void* self, uint8_t c);
    void (*print_dbg)(const char* str);
    void* data;
};
struct SceneReader {
    uint16_t (*read_char)(void* self);
    bool (*eof)(void* self);
    void (*print_dbg)(const char* str);
    void* data;
};

// One private copy of the teletype firmware library, loaded from an in-memory image. The firmware
// keeps its state in globals, so each instance needs its own copy: dlopen of one path twice returns
// the same handle. Not thread-safe; Teletype calls it from one thread at a time.
class Firmware {
public:
    // nullptr on failure, with the reason in err
    static std::unique_ptr<Firmware> load(const void* image, size_t size, std::string& err);
    ~Firmware();

    // GPIO pins, from libavr32's conf/teletype/conf_board.h
    static constexpr uint32_t PIN_TRIG_1 = 0;  // A00..A07: trigger inputs 1-8
    static constexpr uint32_t PIN_TR_1 = 40;   // B08..B11: TR outputs 1-4
    static constexpr uint32_t PIN_FRONT = 13;  // NMI: the front panel button
    // ADC channels, 12-bit
    static constexpr int ADC_IN = 0;
    static constexpr int ADC_PARAM = 1;
    // DAC channel of CV outputs 1-4, 16-bit, 0-10 V (monome-rack's TeletypeModule.cpp)
    static constexpr int DAC_OF_CV[4] = { 2, 3, 0, 1 };
    static constexpr unsigned SCENES = 32;  // SCENE_SLOTS in module/flash.h

    void (*init)() = nullptr;
    void (*step)() = nullptr;
    void (*triggerInterrupt)(int) = nullptr;
    bool (*getGPIO)(uint32_t) = nullptr;
    void (*setGPIO)(uint32_t, bool) = nullptr;
    void (*setADC)(int, uint16_t) = nullptr;
    uint16_t (*getDAC)(int) = nullptr;
    void (*readNVRAM)(void**, uint32_t*) = nullptr;
    void (*writeNVRAM)(const void*, uint32_t) = nullptr;
    void (*readVRAM)(void**, uint32_t*) = nullptr;
    void (*writeVRAM)(const void*, uint32_t) = nullptr;
    void (*setScreenBuffer)(uint8_t*) = nullptr;
    void (*hidConnect)() = nullptr;
    void (*hidMessage)(uint8_t, uint8_t, bool, bool) = nullptr;
    void (*hidFrame)(const uint8_t*, uint8_t) = nullptr;  // plugins/teletype/firmware/ttyp_hid.c
    // a scene slot (0-31) to or from teletype's text format
    void (*serializePreset)(SceneWriter*, uint8_t slot) = nullptr;
    void (*deserializePreset)(SceneReader*, uint8_t slot, bool clearExisting) = nullptr;

private:
    Firmware() = default;
    void* handle_ = nullptr;
    int fd_ = -1;  // the memfd holding the image (Linux)
};

}  // namespace ttyp
