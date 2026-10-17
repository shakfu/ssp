#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace ssp {

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
    unsigned version() const { return version_; }  // changes with the text, not the cursor

    // One change to the text, as tree-sitter's TSInputEdit takes it: bytes from the start of text(), and
    // rows and byte columns, before (old) and after (new) the change.
    struct Edit {
        uint32_t start, oldEnd, newEnd;
        uint32_t startRow, startCol, oldEndRow, oldEndCol, newEndRow, newEndCol;
    };
    static constexpr size_t MAX_EDITS = 256;  // kept; older ones go
    // The edits that took the text from `version` to version(), oldest first; false when they are not
    // all kept, or setText() came between.
    bool editsSince(unsigned version, std::vector<Edit>& out) const;

    // The block around the cursor: its lines up to blank lines outside brackets, strings and comments
    // from `comment` to the line's end; on a blank line between blocks, the block above. First and
    // last row, or false if there is none.
    bool block(int& first, int& last, const std::string& comment = ";") const;
    std::string lines(int first, int last) const;

    // the printable character for a usage, 0 for none
    static char character(uint8_t usage, bool shift);

private:
    void insert(const std::string& s);
    void newline();
    void backspace();
    void del();
    void clampCol();
    uint32_t offset(int row, int col) const;  // the byte in text()
    // before a change: from row0:col0 to row1:col1 becomes `inserted` bytes ending at row2:col2
    void record(int row0, int col0, int row1, int col1, size_t inserted, int row2, int col2);

    std::vector<std::string> lines_;
    int row_ = 0, col_ = 0;
    int goalCol_ = 0;  // the column up and down aim for, across short lines
    unsigned version_ = 0;
    std::deque<Edit> edits_;  // the last ones, up to version_
};

}  // namespace ssp
