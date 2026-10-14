#include "TextBuffer.h"

#include <algorithm>

namespace sapf {

namespace {

// HID usages, page 0x07
enum : uint8_t {
    K_A = 0x04, K_Z = 0x1D, K_1 = 0x1E, K_0 = 0x27, K_ENTER = 0x28, K_ESC = 0x29, K_BACKSPACE = 0x2A,
    K_TAB = 0x2B, K_SPACE = 0x2C, K_SLASH = 0x38, K_DOT = 0x37, K_HOME = 0x4A, K_PAGEUP = 0x4B,
    K_DELETE = 0x4C, K_END = 0x4D, K_PAGEDOWN = 0x4E, K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51,
    K_UP = 0x52, K_S = 0x16, K_O = 0x12
};
constexpr int PAGE = 10;  // rows PageUp and PageDown move

bool blank(const std::string& s) {
    return s.find_first_not_of(" \t\r") == std::string::npos;
}

}  // namespace

char TextBuffer::character(uint8_t u, bool shift) {
    if (u >= K_A && u <= K_Z) return char((shift ? 'A' : 'a') + (u - K_A));
    if (u >= K_1 && u <= K_0) return (shift ? "!@#$%^&*()" : "1234567890")[u - K_1];
    switch (u) {
        case K_SPACE: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case K_DOT: return shift ? '>' : '.';
        case K_SLASH: return shift ? '?' : '/';
        default: return 0;
    }
}

TextBuffer::Action TextBuffer::key(uint8_t u, uint8_t mods) {
    const bool ctrl = mods & CTRL, shift = mods & SHIFT;
    if (ctrl) {
        switch (u) {
            case K_ENTER: return EVALUATE;
            case K_DOT: return STOP;
            case K_S: return SAVE;
            case K_O: return OPEN;
            case K_HOME: row_ = col_ = goalCol_ = 0; return NONE;
            case K_END:
                row_ = int(lines_.size()) - 1;
                col_ = goalCol_ = int(lines_[row_].size());
                return NONE;
            default: return NONE;  // other chords type nothing
        }
    }
    bool vertical = false;
    switch (u) {
        case K_ESC: return STOP;
        case K_ENTER: newline(); break;
        case K_BACKSPACE: backspace(); break;
        case K_DELETE: del(); break;
        case K_TAB: insert(std::string(size_t(TAB - col_ % TAB), ' ')); break;
        case K_LEFT:
            if (col_ > 0) col_--;
            else if (row_ > 0) col_ = int(lines_[--row_].size());
            break;
        case K_RIGHT:
            if (col_ < int(lines_[row_].size())) col_++;
            else if (row_ + 1 < int(lines_.size())) row_++, col_ = 0;
            break;
        case K_UP: row_ = std::max(row_ - 1, 0), vertical = true; break;
        case K_DOWN: row_ = std::min(row_ + 1, int(lines_.size()) - 1), vertical = true; break;
        case K_PAGEUP: row_ = std::max(row_ - PAGE, 0), vertical = true; break;
        case K_PAGEDOWN: row_ = std::min(row_ + PAGE, int(lines_.size()) - 1), vertical = true; break;
        case K_HOME: col_ = 0; break;
        case K_END: col_ = int(lines_[row_].size()); break;
        default:
            if (char c = character(u, shift)) insert(std::string(1, c));
            break;
    }
    if (vertical) {
        col_ = goalCol_;
        clampCol();
    } else {
        goalCol_ = col_;
    }
    return NONE;
}

void TextBuffer::clampCol() {
    col_ = std::min(col_, int(lines_[row_].size()));
}

void TextBuffer::insert(const std::string& s) {
    lines_[row_].insert(size_t(col_), s);
    col_ += int(s.size());
}

void TextBuffer::newline() {
    std::string rest = lines_[row_].substr(size_t(col_));
    lines_[row_].erase(size_t(col_));
    // keep the line's indent
    size_t indent = lines_[row_].find_first_not_of(' ');
    std::string pad(indent == std::string::npos ? lines_[row_].size() : indent, ' ');
    lines_.insert(lines_.begin() + row_ + 1, pad + rest);
    row_++;
    col_ = int(pad.size());
}

void TextBuffer::backspace() {
    if (col_ > 0) {
        lines_[row_].erase(size_t(--col_), 1);
    } else if (row_ > 0) {
        col_ = int(lines_[row_ - 1].size());
        lines_[row_ - 1] += lines_[row_];
        lines_.erase(lines_.begin() + row_--);
    }
}

void TextBuffer::del() {
    if (col_ < int(lines_[row_].size())) {
        lines_[row_].erase(size_t(col_), 1);
    } else if (row_ + 1 < int(lines_.size())) {
        lines_[row_] += lines_[row_ + 1];
        lines_.erase(lines_.begin() + row_ + 1);
    }
}

std::string TextBuffer::text() const {
    return lines(0, int(lines_.size()) - 1);
}

void TextBuffer::setText(const std::string& text) {
    lines_.clear();
    size_t start = 0;
    for (;;) {
        size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // tabs to spaces, as the tab key inserts them
        std::string expanded;
        for (char c : line) {
            if (c == '\t') expanded.append(size_t(TAB - expanded.size() % TAB), ' ');
            else expanded += c;
        }
        lines_.push_back(expanded);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    row_ = col_ = goalCol_ = 0;
}

std::string TextBuffer::lines(int first, int last) const {
    std::string s;
    for (int r = first; r <= last; r++) {
        s += lines_[size_t(r)];
        if (r < last) s += '\n';
    }
    return s;
}

bool TextBuffer::block(int& first, int& last) const {
    // the blocks, as the harness splits files: a blank line ends one only outside brackets and strings
    int depth = 0, start = -1;
    bool quoted = false;
    std::vector<std::pair<int, int>> blocks;
    for (int r = 0; r < int(lines_.size()); r++) {
        const std::string& s = lines_[size_t(r)];
        if (blank(s)) {
            if (depth > 0 || quoted || start < 0) continue;
            blocks.push_back({ start, r - 1 });
            start = -1;
            depth = 0;
            continue;
        }
        if (start < 0) start = r;
        for (char c : s) {
            if (quoted) quoted = c != '"';
            else if (c == ';') break;
            else if (c == '"') quoted = true;
            else if (c == '(' || c == '[' || c == '{') depth++;
            else if (c == ')' || c == ']' || c == '}') depth--;
        }
    }
    if (start >= 0) {
        int end = int(lines_.size()) - 1;
        while (end > start && blank(lines_[size_t(end)])) end--;
        blocks.push_back({ start, end });
    }
    const std::pair<int, int>* above = nullptr;
    for (const auto& b : blocks) {
        if (row_ >= b.first && row_ <= b.second) {
            first = b.first, last = b.second;
            return true;
        }
        if (b.second < row_) above = &b;
    }
    if (!above) return false;
    first = above->first, last = above->second;
    return true;
}

}  // namespace sapf
