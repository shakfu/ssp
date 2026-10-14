#include "FilePicker.h"

#include <algorithm>

namespace sapf {

void FilePicker::show(std::vector<std::string> names, const std::string& current) {
    names_ = std::move(names);
    auto it = std::find(names_.begin(), names_.end(), current);
    sel_ = it == names_.end() ? 0 : int(it - names_.begin());
    shown_ = true;
}

void FilePicker::move(int by) {
    sel_ = std::clamp(sel_ + by, 0, std::max(0, int(names_.size()) - 1));
}

FilePicker::Result FilePicker::key(uint8_t usage) {
    switch (usage) {
        case 0x28: return names_.empty() ? CANCEL : OPEN;  // Enter
        case 0x29: return CANCEL;                           // Esc
        case 0x52: move(-1); break;                         // Up
        case 0x51: move(1); break;                          // Down
        case 0x4B: move(-10); break;                        // PageUp
        case 0x4E: move(10); break;                         // PageDown
        case 0x4A: move(-int(names_.size())); break;        // Home
        case 0x4D: move(int(names_.size())); break;         // End
        default: break;
    }
    return NONE;
}

}  // namespace sapf
