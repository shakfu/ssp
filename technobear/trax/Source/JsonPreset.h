#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

// Shared parsing for JSON presets, described in ../README.md. tools/py2trax reads and writes
// the same schema, so the two must agree on slot names, jack syntax and parameter units.
namespace jsonpreset {

static constexpr int BAD_INDEX = -1;

// "in" -> M_IN, "out" -> M_OUT, "1".."8" -> the matching slot. BAD_INDEX if unrecognised.
int slotIndex(const juce::String& name);
juce::String slotName(int index);

// A channel is either an index or one of the module's own channel names, so it can only be
// resolved once the module is loaded. BAD_INDEX if out of range or unknown.
int channelIndex(const juce::String& channel, const std::vector<std::string>& names);

struct Jack {
    int slot = BAD_INDEX;
    juce::String channel;
    bool valid() const { return slot != BAD_INDEX && channel.isNotEmpty(); }
};

struct Wire {
    Jack src, dest;
    float gain = 1.0f;
    float offset = 0.0f;
    bool valid() const { return src.valid() && dest.valid(); }
};

// "<slot>:<channel>", e.g. "1:0", "out:1" or "2:AS Trig".
Jack parseJack(const juce::String& text);

// Either "1:0 -> 2:4" or { "from": "1:0", "to": "2:4", "gain": 1.0, "offset": 0.0 }.
Wire parseWire(const juce::var& wire);

// True when the file starts with '{', which no JUCE binary preset does.
bool isJsonFile(const juce::File& file);
bool isJsonName(const juce::String& name);

void logError(const juce::String& message);

}  // namespace jsonpreset
