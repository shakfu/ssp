// The "matrix" field of JSON presets: parsing, saving, and a load-save-load through Track.
// Built by CMakeLists.txt and run from a directory holding plugins/pass2.so and plugins/pass6.so.

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "JsonPreset.h"
#include "Track.h"

static int failures = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

using jsonpreset::Wire;

static bool sameJack(const jsonpreset::Jack& j, int slot, const char* channel) {
    return j.slot == slot && j.channel == channel;
}

static void parseExpandsCellsAndDc() {
    juce::String error;
    auto wires = jsonpreset::parseMatrix(juce::JSON::parse(R"({
        "rows": ["in:0", "in:1", "dc"],
        "cols": ["1:In 0", "1:1"],
        "gain": [[1, 0], [0.5, 1], [0.25, 0]] })"),
                                         error);
    CHECK(error.isEmpty());
    CHECK(wires.size() == 3);
    if (wires.size() != 3) return;
    // dc lands on the first wire into column 0 only
    CHECK(sameJack(wires[0].src, Track::M_IN, "0") && sameJack(wires[0].dest, 1, "In 0"));
    CHECK(wires[0].gain == 1.0f && wires[0].offset == 0.25f);
    CHECK(sameJack(wires[1].src, Track::M_IN, "1") && sameJack(wires[1].dest, 1, "In 0"));
    CHECK(wires[1].gain == 0.5f && wires[1].offset == 0.0f);
    CHECK(sameJack(wires[2].dest, 1, "1") && wires[2].gain == 1.0f);
}

static void parseRejectsMalformed() {
    const char* bad[] = {
        R"({ "rows": ["in:0"], "cols": ["out:0"] })",                                  // no gain
        R"({ "rows": ["in:0"], "cols": ["out:0"], "gain": [[1], [1]] })",              // rows
        R"({ "rows": ["in:0"], "cols": ["out:0", "out:1"], "gain": [[1]] })",          // columns
        R"({ "rows": ["in:0"], "cols": ["out:0", "dc"], "gain": [[1, 1]] })",          // dc column
        R"({ "rows": ["in:0", "IN:0"], "cols": ["out:0"], "gain": [[1], [1]] })",      // repeated
        R"({ "rows": ["in0"], "cols": ["out:0"], "gain": [[1]] })",                    // bad jack
        R"({ "rows": ["in:0"], "cols": ["out:0"], "gain": [["x"]] })",                 // not a number
        R"({ "rows": ["in:0", "dc"], "cols": ["out:0", "out:1"], "gain": [[1, 0], [0, 1]] })",  // dc alone
        R"([1, 2])",                                                                   // not an object
    };
    for (auto* text : bad) {
        juce::String error;
        auto wires = jsonpreset::parseMatrix(juce::JSON::parse(text), error);
        CHECK(error.isNotEmpty());
        CHECK(wires.empty());
        if (error.isEmpty()) std::fprintf(stderr, "  accepted: %s\n", text);
    }
}

static void formatSumsAndOrders() {
    // out of order, a repeated wire, and two offsets into one column
    std::vector<Wire> wires = {
        { { 2, "0" }, { Track::M_OUT, "0" }, 0.1f, 0.05f },
        { { Track::M_IN, "1" }, { 2, "0" }, 1.0f, 0.0f },
        { { 2, "0" }, { Track::M_OUT, "0" }, 0.2f, 0.05f },
    };
    auto text = juce::JSON::toString(jsonpreset::formatMatrix(wires), true);
    CHECK(text == R"({"rows": ["in:1", "2:0", "dc"], "cols": ["2:0", "out:0"], "gain": [[1.0, 0.0], [0.0, 0.3], [0.0, 0.1]]})");
    if (failures) std::fprintf(stderr, "  formatted: %s\n", text.toRawUTF8());
}

using Conn = std::tuple<unsigned, unsigned, unsigned, unsigned, float, float>;

static std::vector<Conn> connections(Track& track) {
    std::vector<Conn> out;
    for (auto& w : track.connections()) {
        out.emplace_back(w.src_.modIdx_, w.src_.chIdx_, w.dest_.modIdx_, w.dest_.chIdx_, w.gain_, w.offset_);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static void trackLoadsSavesAndReloads() {
    auto preset = juce::JSON::parse(R"({
        "modules": { "1": "pass2", "2": "pass6" },
        "wires": [ "in:2 -> 2:5" ],
        "matrix": {
            "rows": ["in:0", "1:Out 0", "1:Out 1", "dc"],
            "cols": ["1:In 0", "2:In 1", "out:0"],
            "gain": [[1, 0, 0], [0, 0.5, 0], [0, 0, 1], [0, 0, 0.1]] } })");

    Track track;
    track.prepare(48000, 128);
    track.setStateInformation(preset, 0);
    auto loaded = connections(track);
    std::vector<Conn> expected = {
        { Track::M_IN, 0, 1, 0, 1.0f, 0.0f },
        { Track::M_IN, 2, 2, 5, 1.0f, 0.0f },  // from "wires", which still loads
        { 1, 0, 2, 1, 0.5f, 0.0f },
        { 1, 1, Track::M_OUT, 0, 1.0f, 0.1f },
    };
    CHECK(loaded == expected);

    juce::var saved;
    track.getStateInformation(saved);
    CHECK(saved.getProperty("wires", juce::var()).isVoid());
    CHECK(saved.getProperty("matrix", juce::var()).isObject());

    Track reloaded;
    reloaded.prepare(48000, 128);
    reloaded.setStateInformation(saved, 0);
    CHECK(connections(reloaded) == loaded);
}

int main() {
    parseExpandsCellsAndDc();
    parseRejectsMalformed();
    formatSumsAndOrders();
    trackLoadsSavesAndReloads();
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
