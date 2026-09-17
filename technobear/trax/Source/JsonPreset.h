#pragma once

#include <juce_core/juce_core.h>

#include <string>

// Shared parsing for JSON presets. The format is described in tools/py2trax/README.md;
// py2trax generates the binary presets this mirrors, so the two must agree on slot names,
// jack syntax and parameter units.
namespace jsonpreset {

static constexpr int BAD_INDEX = -1;

// "in" -> M_IN, "out" -> M_OUT, "1".."8" -> the matching slot. BAD_INDEX if unrecognised.
int slotIndex(const juce::String& name);
juce::String slotName(int index);

struct Jack {
    int slot = BAD_INDEX;
    int channel = BAD_INDEX;
    bool valid() const { return slot != BAD_INDEX && channel >= 0; }
};

struct Wire {
    Jack src, dest;
    float gain = 1.0f;
    float offset = 0.0f;
    bool valid() const { return src.valid() && dest.valid(); }
};

// "<slot>:<channel>", e.g. "1:0" or "out:1".
Jack parseJack(const juce::String& text);

// Either "1:0 -> 2:4" or { "from": "1:0", "to": "2:4", "gain": 1.0, "offset": 0.0 }.
Wire parseWire(const juce::var& wire);

// True when the file starts with '{', which no JUCE binary preset does.
bool isJsonFile(const juce::File& file);

void logError(const juce::String& message);

}  // namespace jsonpreset
