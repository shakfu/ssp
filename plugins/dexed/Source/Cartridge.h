#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dexed {

// A DX7 cartridge: 32 voices, unpacked to Dexed's 155-byte layout. No JUCE, so tests build natively.
struct Cartridge {
    static constexpr int VOICES = 32;
    static constexpr int VOICE_BYTES = 155;  // unpacked: 6 operators x 21 (operator 6 first), then 29 global
    static constexpr int PACKED_BYTES = 128;
    static constexpr int BANK_BYTES = VOICES * PACKED_BYTES;  // 4096
    static constexpr int SYSEX_BYTES = BANK_BYTES + 8;        // F0 43 0n 09 20 00, data, checksum, F7

    uint8_t voice[VOICES][VOICE_BYTES] = {};
    bool checksumOk = true;  // a bulk dump whose checksum fails still loads, as in Dexed

    // The voice's 10-character name, trailing spaces removed, non-ASCII as spaces.
    std::string name(int v) const;

    // A 4104-byte bulk dump, alone or inside a longer file, or the 4096 bytes of its data.
    // Empty on success, else the reason; on failure the cartridge is unchanged.
    std::string parse(const uint8_t* data, size_t size);
    std::string load(const std::string& path);

    // One voice from the bank's 128-byte packed form, sanitized.
    static void unpack(const uint8_t* packed, uint8_t* voice);
    // Clamps each byte to its parameter's range: the engine indexes tables with them.
    static void sanitize(uint8_t* voice);
};

}  // namespace dexed
