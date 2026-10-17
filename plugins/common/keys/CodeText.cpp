#include "keys/CodeText.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ssp {

// on black, by Highlighter::Kind
static juce::Colour colourOf(char kind) {
    static const juce::Colour palette[] = {
        juce::Colours::white,         // PLAIN
        juce::Colour(130, 130, 130),  // COMMENT
        juce::Colour(152, 195, 121),  // STRING
        juce::Colour(209, 154, 102),  // NUMBER
        juce::Colour(198, 120, 221),  // KEYWORD
        juce::Colour(229, 192, 123),  // TYPE
        juce::Colour(97, 175, 239),   // FUNCTION
        juce::Colour(86, 182, 194),   // BUILTIN
        juce::Colour(255, 214, 140),  // OPERATOR: the editor's amber
        juce::Colour(150, 150, 150),  // PUNCTUATION
    };
    const auto k = size_t(uint8_t(kind));
    return k < sizeof(palette) / sizeof(palette[0]) ? palette[k] : palette[0];
}

void drawCode(juce::Graphics& g, const TextBuffer& text, juce::Rectangle<int> area, int lineH, int& top,
              int flashFirst, int flashLast, const std::vector<std::string>* kinds) {
    const float charW = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), "M");
    const int cols = std::max(1, int(area.getWidth() / charW));
    const int shown = std::max(1, area.getHeight() / lineH);
    const auto& lines = text.lines();

    // screen rows: each line in pieces of `cols` characters
    struct Row {
        int line, start;
    };
    std::vector<Row> rows;
    int cursorRow = 0;
    for (int r = 0; r < int(lines.size()); r++) {
        const int len = int(lines[size_t(r)].size()), pieces = std::max(1, (len + cols - 1) / cols);
        if (r == text.row()) cursorRow = int(rows.size()) + std::min(text.col() / cols, pieces - 1);
        for (int k = 0; k < pieces; k++) rows.push_back({ r, k * cols });
    }
    top = std::clamp(top, cursorRow - shown + 1, cursorRow);

    for (int i = 0; i < shown && top + i < int(rows.size()); i++) {
        const Row& row = rows[size_t(top + i)];
        const int y = area.getY() + i * lineH;
        if (row.line >= flashFirst && row.line <= flashLast) {
            g.setColour(juce::Colour(30, 50, 90));
            g.fillRect(area.getX(), y, area.getWidth(), lineH);
        }
        const std::string& line = lines[size_t(row.line)];
        const std::string* k = kinds && size_t(row.line) < kinds->size() ? &(*kinds)[size_t(row.line)] : nullptr;
        if (!k || k->size() != line.size()) {
            g.setColour(juce::Colours::white);
            g.drawText(juce::String(line.substr(size_t(row.start), size_t(cols))), area.getX(), y, area.getWidth(),
                       lineH, juce::Justification::left, false);
            continue;
        }
        // runs of one kind, each at its column
        const int end = std::min(int(line.size()), row.start + cols);
        for (int a = row.start, b; a < end; a = b) {
            for (b = a + 1; b < end && (*k)[size_t(b)] == (*k)[size_t(a)];) b++;
            if (line.find_first_not_of(' ', size_t(a)) >= size_t(b)) continue;  // spaces only
            const int x = area.getX() + int(std::round((a - row.start) * charW));
            g.setColour(colourOf((*k)[size_t(a)]));
            g.drawText(juce::String(line.substr(size_t(a), size_t(b - a))), x, y, area.getRight() - x, lineH,
                       juce::Justification::left, false);
        }
    }
    // the cursor, a bar before its column
    if (cursorRow >= top && cursorRow < top + shown) {
        const int x = area.getX() + int(std::round((text.col() - rows[size_t(cursorRow)].start) * charW));
        g.setColour(juce::Colour(255, 214, 140));
        g.fillRect(x - 1, area.getY() + (cursorRow - top) * lineH + 2, 3, lineH - 4);
    }
}

}  // namespace ssp
