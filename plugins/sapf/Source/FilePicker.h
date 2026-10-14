#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sapf {

// Ctrl-O's list of files to open: a selection moved by HID key usages (page 0x07). No JUCE, so it tests
// natively.
class FilePicker {
public:
    enum Result { NONE, OPEN, CANCEL };

    void show(std::vector<std::string> names, const std::string& current);  // selects `current` if listed
    void hide() { shown_ = false; }
    bool shown() const { return shown_; }

    Result key(uint8_t usage);
    void move(int by);  // the selection, clamped to the list

    const std::vector<std::string>& names() const { return names_; }
    int selected() const { return sel_; }

private:
    std::vector<std::string> names_;
    int sel_ = 0;
    bool shown_ = false;
};

}  // namespace sapf
