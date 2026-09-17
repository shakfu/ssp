#include "JsonPreset.h"

#include "Track.h"
#include "ssp/Log.h"

namespace jsonpreset {

int slotIndex(const juce::String& name) {
    auto key = name.trim().toLowerCase();
    if (key == "in") return Track::M_IN;
    if (key == "out") return Track::M_OUT;
    if (!key.containsOnly("0123456789") || key.isEmpty()) return BAD_INDEX;

    int slot = key.getIntValue();
    return (slot >= Track::M_SLOT_1 && slot < Track::M_OUT) ? slot : BAD_INDEX;
}

juce::String slotName(int index) {
    if (index == Track::M_IN) return "in";
    if (index == Track::M_OUT) return "out";
    return juce::String(index);
}

Jack parseJack(const juce::String& text) {
    Jack jack;
    int colon = text.indexOfChar(':');
    if (colon < 0) return jack;

    auto channel = text.substring(colon + 1).trim();
    if (channel.isEmpty() || !channel.containsOnly("0123456789")) return jack;

    jack.slot = slotIndex(text.substring(0, colon));
    jack.channel = channel.getIntValue();
    return jack;
}

Wire parseWire(const juce::var& wire) {
    Wire parsed;

    if (wire.isString()) {
        auto text = wire.toString();
        int arrow = text.indexOf("->");
        if (arrow < 0) return parsed;
        parsed.src = parseJack(text.substring(0, arrow));
        parsed.dest = parseJack(text.substring(arrow + 2));
        return parsed;
    }

    if (auto* object = wire.getDynamicObject()) {
        parsed.src = parseJack(object->getProperty("from").toString());
        parsed.dest = parseJack(object->getProperty("to").toString());
        if (object->hasProperty("gain")) parsed.gain = (float)(double)object->getProperty("gain");
        if (object->hasProperty("offset")) parsed.offset = (float)(double)object->getProperty("offset");
    }
    return parsed;
}

bool isJsonFile(const juce::File& file) {
    juce::FileInputStream stream(file);
    if (!stream.openedOk()) return false;

    // The binary presets start with the copyXmlToBinary magic number, never with '{'.
    for (int i = 0; i < 16 && !stream.isExhausted(); i++) {
        auto c = (char)stream.readByte();
        if (!juce::CharacterFunctions::isWhitespace(c)) return c == '{';
    }
    return false;
}

void logError(const juce::String& message) {
    ssp::log("json preset : " + message.toStdString());
}

}  // namespace jsonpreset
