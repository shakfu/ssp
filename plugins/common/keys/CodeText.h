#pragma once

#include <juce_graphics/juce_graphics.h>

#include "keys/TextBuffer.h"

namespace ssp {

// Draws `text` in the current font, soft-wrapped to `area`, one row per `lineH` px, with the cursor as a
// bar. `top`, the first screen row shown, scrolls to keep the cursor in view. Lines flashFirst..flashLast
// get a highlight. `kinds`, a Highlighter::Kind per byte line by line, colours the text; null for white.
void drawCode(juce::Graphics& g, const TextBuffer& text, juce::Rectangle<int> area, int lineH, int& top,
              int flashFirst = 0, int flashLast = -1, const std::vector<std::string>* kinds = nullptr);

}  // namespace ssp
