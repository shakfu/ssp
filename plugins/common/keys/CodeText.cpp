#include "keys/CodeText.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ssp {

void drawCode(juce::Graphics& g, const TextBuffer& text, juce::Rectangle<int> area, int lineH, int& top,
              int flashFirst, int flashLast) {
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
        g.setColour(juce::Colours::white);
        g.drawText(juce::String(lines[size_t(row.line)].substr(size_t(row.start), size_t(cols))), area.getX(), y,
                   area.getWidth(), lineH, juce::Justification::left, false);
    }
    // the cursor, a bar before its column
    if (cursorRow >= top && cursorRow < top + shown) {
        const int x = area.getX() + int(std::round((text.col() - rows[size_t(cursorRow)].start) * charW));
        g.setColour(juce::Colour(255, 214, 140));
        g.fillRect(x - 1, area.getY() + (cursorRow - top) * lineH + 2, 3, lineH - 4);
    }
}

}  // namespace ssp
