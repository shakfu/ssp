#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace radio {

// One playable file: a headerless 16-bit mono .raw (RadioMusic's format) or a PCM/float .wav.
struct Station {
    enum Format { PCM16, PCM24, PCM32, FLOAT32 };
    std::string path, name;
    uint64_t frames = 0;
    uint32_t rate = 0;  // 0 for .raw: the caller supplies the rate
    uint16_t channels = 1;
    Format format = PCM16;
    uint64_t dataOffset = 0;
    unsigned bytesPerFrame() const;
};

// Fills `st` from the file's header (or size, for .raw). False if unreadable or unsupported.
bool probe(const std::string& path, Station& st);

// Name order: case-insensitive, with digit runs compared as numbers, so "2" sorts before "10".
bool nameLess(const std::string& a, const std::string& b);

// The stations in `dir`, in name order.
std::vector<Station> scanBank(const std::string& dir);

// The banks under `root`: its subdirectories, in name order. If it has none, root itself.
std::vector<std::string> scanBanks(const std::string& root);

// The Radio Music settings this plugin uses, from SETTINGS.TXT in `root`; -1 where absent.
// Keys are case-insensitive. crossfadeTime, or DECLICK on older firmware, sets the fade.
struct Settings {
    int fadeMs = -1;
    int startPotImmediate = -1;
    int startCvImmediate = -1;
};
Settings readSettings(const std::string& root);

// Sequential reader that loops at the end of the file and mixes channels to mono.
class Reader {
public:
    Reader() = default;
    ~Reader() { close(); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool open(const Station& st, uint64_t frame);
    void close();
    // Reads n frames; returns fewer only on a read error.
    size_t read(float* dst, size_t n);

private:
    bool seek(uint64_t frame);
    Station st_;
    FILE* f_ = nullptr;
    uint64_t pos_ = 0;
    std::vector<uint8_t> raw_;
};

}  // namespace radio
