#include "JsonPreset.h"

#include <algorithm>
#include <cmath>
#include <map>

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

int channelIndex(const juce::String& channel, const std::vector<std::string>& names) {
    if (channel.containsOnly("0123456789") && channel.isNotEmpty()) {
        int index = channel.getIntValue();
        return index < (int)names.size() ? index : BAD_INDEX;
    }

    for (size_t index = 0; index < names.size(); index++) {
        if (channel.equalsIgnoreCase(names[index].c_str())) return (int)index;
    }
    return BAD_INDEX;
}

Jack parseJack(const juce::String& text) {
    Jack jack;
    int colon = text.indexOfChar(':');
    if (colon < 0) return jack;

    jack.slot = slotIndex(text.substring(0, colon));
    jack.channel = text.substring(colon + 1).trim();
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

static bool isDc(const juce::var& label) {
    return label.toString().trim().equalsIgnoreCase("dc");
}

// Labels as jacks; a "dc" row is allowed once. Repeated labels are an error.
static bool parseLabels(const juce::Array<juce::var>& list, bool isRow, std::vector<Jack>& jacks, int& dcIdx,
                        juce::String& error) {
    juce::StringArray seen;
    for (int i = 0; i < list.size(); i++) {
        auto label = list[i].toString().trim();
        if (seen.contains(label, true)) {
            error = "matrix : repeated label " + label.quoted();
            return false;
        }
        seen.add(label);

        if (isDc(list[i])) {
            if (!isRow) {
                error = "matrix : 'dc' is a source, so it can only label a row";
                return false;
            }
            dcIdx = i;
            jacks.push_back({});
            continue;
        }
        auto jack = parseJack(label);
        if (!jack.valid()) {
            error = "matrix : bad jack " + label.quoted();
            return false;
        }
        jacks.push_back(jack);
    }
    return true;
}

std::vector<Wire> parseMatrix(const juce::var& matrix, juce::String& error) {
    auto* object = matrix.getDynamicObject();
    auto* rows = object ? object->getProperty("rows").getArray() : nullptr;
    auto* cols = object ? object->getProperty("cols").getArray() : nullptr;
    auto* gain = object ? object->getProperty("gain").getArray() : nullptr;
    if (rows == nullptr || cols == nullptr || gain == nullptr) {
        error = "matrix : needs rows, cols and gain arrays";
        return {};
    }

    std::vector<Jack> srcs, dests;
    int dcRow = BAD_INDEX, unused = BAD_INDEX;
    if (!parseLabels(*rows, true, srcs, dcRow, error) || !parseLabels(*cols, false, dests, unused, error)) return {};
    if (gain->size() != rows->size()) {
        error = "matrix : " + juce::String(gain->size()) + " rows of gain for " + juce::String(rows->size()) + " rows";
        return {};
    }

    std::vector<std::vector<float>> cells;
    for (int r = 0; r < gain->size(); r++) {
        auto* row = (*gain)[r].getArray();
        if (row == nullptr || row->size() != cols->size()) {
            error = "matrix : row " + (*rows)[r].toString().quoted() + " needs " + juce::String(cols->size()) +
                    " values";
            return {};
        }
        cells.emplace_back();
        for (auto& v : *row) {
            double value = (double)v;
            if (!(v.isInt() || v.isInt64() || v.isDouble()) || !std::isfinite(value)) {
                error = "matrix : row " + (*rows)[r].toString().quoted() + " has a value that is not a finite number";
                return {};
            }
            cells.back().push_back((float)value);
        }
    }

    std::vector<Wire> wires;
    std::vector<int> firstInto(dests.size(), BAD_INDEX);
    for (size_t r = 0; r < srcs.size(); r++) {
        if ((int)r == dcRow) continue;
        for (size_t c = 0; c < dests.size(); c++) {
            if (cells[r][c] == 0.0f) continue;
            if (firstInto[c] == BAD_INDEX) firstInto[c] = (int)wires.size();
            wires.push_back({ srcs[r], dests[c], cells[r][c], 0.0f });
        }
    }

    if (dcRow != BAD_INDEX) {
        for (size_t c = 0; c < dests.size(); c++) {
            if (cells[dcRow][c] == 0.0f) continue;
            if (firstInto[c] == BAD_INDEX) {
                error = "matrix : column " + (*cols)[(int)c].toString().quoted() +
                        " has a dc offset but no wire into it";
                return {};
            }
            wires[firstInto[c]].offset = cells[dcRow][c];
        }
    }
    return wires;
}

// floats print as 0.100000001490116 through var's double; 6 decimals keep the file readable
static juce::var tidy(double v) {
    return std::round(v * 1e6) / 1e6;
}

juce::var formatMatrix(const std::vector<Wire>& wires) {
    using Key = std::pair<int, int>;  // slot, channel index
    std::map<Key, int> srcIdx, destIdx;
    for (auto& w : wires) {
        srcIdx[{ w.src.slot, w.src.channel.getIntValue() }] = 0;
        destIdx[{ w.dest.slot, w.dest.channel.getIntValue() }] = 0;
    }

    juce::Array<juce::var> rows, cols;
    for (auto& [key, idx] : srcIdx) {
        idx = rows.size();
        rows.add(slotName(key.first) + ":" + juce::String(key.second));
    }
    for (auto& [key, idx] : destIdx) {
        idx = cols.size();
        cols.add(slotName(key.first) + ":" + juce::String(key.second));
    }

    std::vector<std::vector<double>> cells(rows.size(), std::vector<double>(cols.size(), 0.0));
    std::vector<double> dc(cols.size(), 0.0);
    for (auto& w : wires) {
        int c = destIdx[{ w.dest.slot, w.dest.channel.getIntValue() }];
        cells[srcIdx[{ w.src.slot, w.src.channel.getIntValue() }]][c] += w.gain;
        dc[c] += w.offset;
    }

    juce::Array<juce::var> gain;
    for (auto& row : cells) {
        juce::Array<juce::var> values;
        for (double v : row) values.add(tidy(v));
        gain.add(values);
    }
    if (std::any_of(dc.begin(), dc.end(), [](double v) { return v != 0.0; })) {
        rows.add("dc");
        juce::Array<juce::var> values;
        for (double v : dc) values.add(tidy(v));
        gain.add(values);
    }

    auto* object = new juce::DynamicObject();
    object->setProperty("rows", rows);
    object->setProperty("cols", cols);
    object->setProperty("gain", gain);
    return juce::var(object);
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

bool isJsonName(const juce::String& name) {
    return name.endsWithIgnoreCase(".json");
}

void logError(const juce::String& message) {
    ssp::log("json preset : " + message.toStdString());
}

}  // namespace jsonpreset
