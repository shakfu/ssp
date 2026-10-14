#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sapf {

// The editor's text and cursor, edited by HID key usages (page 0x07) on a US layout. No JUCE, so it
// tests natively. Lines hold no newline; a tab key inserts spaces.
class TextBuffer {
public:
    // what a key asks the plugin to do beyond editing
    enum Action { NONE, EVALUATE, STOP, SAVE, OPEN };
    enum Mod : uint8_t { CTRL = 1, SHIFT = 2, ALT = 4 };
    static constexpr int TAB = 4;

    TextBuffer() : lines_{ "" } {}

    // a key that is not a modifier; `mods` is the CTRL | SHIFT | ALT state
    Action key(uint8_t usage, uint8_t mods);

    std::string text() const;
    void setText(const std::string& text);  // cursor to the start
    const std::vector<std::string>& lines() const { return lines_; }
    int row() const { return row_; }
    int col() const { return col_; }

    // The block Ctrl-Enter evaluates: the lines around the cursor up to blank lines outside brackets
    // and strings; on a blank line between blocks, the block above. First and last row, or false if
    // there is none.
    bool block(int& first, int& last) const;
    std::string lines(int first, int last) const;

    // the printable character for a usage, 0 for none
    static char character(uint8_t usage, bool shift);

private:
    void insert(const std::string& s);
    void newline();
    void backspace();
    void del();
    void clampCol();

    std::vector<std::string> lines_;
    int row_ = 0, col_ = 0;
    int goalCol_ = 0;  // the column up and down aim for, across short lines
};

}  // namespace sapf
