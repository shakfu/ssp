// The "matrix" field of JSON presets: parsing, saving, a load-save-load through Track, and the
// wire edits the routing grid makes.
// Built by CMakeLists.txt and run from a directory holding plugins/pass2.so and plugins/pass6.so.
// argv[1], if given, is a directory of presets whose every track matrix must parse.

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

static void formatDropsZeroCells() {
    std::vector<Wire> wires = {
        { { 1, "0" }, { 2, "0" }, 0.5f, 0.0f },
        { { 1, "1" }, { 2, "1" }, 0.0f, 0.0f },   // gain 0: no wire, no labels
        { { 1, "1" }, { 2, "0" }, 0.0f, 0.25f },  // gain 0, offset into a wired column: dc kept
        { { 3, "0" }, { 2, "2" }, 0.0f, 0.5f },   // gain 0, offset into an unwired column: dropped
        { { 3, "1" }, { Track::M_OUT, "0" }, 0.5f, 0.0f },
        { { 3, "1" }, { Track::M_OUT, "0" }, -0.5f, 0.0f },  // cancels the wire above
    };
    auto text = juce::JSON::toString(jsonpreset::formatMatrix(wires), true);
    CHECK(text == R"({"rows": ["1:0", "dc"], "cols": ["2:0"], "gain": [[0.5], [0.25]]})");
    if (failures) std::fprintf(stderr, "  formatted: %s\n", text.toRawUTF8());

    // nothing audible left: no matrix at all
    CHECK(jsonpreset::formatMatrix({ { { 1, "0" }, { 2, "0" }, 0.0f, 0.0f } }).isVoid());
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

static void trackTogglesWires() {
    Track track;
    track.prepare(48000, 128);
    track.setStateInformation(juce::JSON::parse(R"({ "modules": { "1": "pass2", "2": "pass6" } })"), 0);
    Matrix::Jack src(1, 1), dest(2, 4);

    while (!track.requestMatrixToggle(src, dest)) {}
    std::vector<Conn> one = { { 1, 1, 2, 4, 1.0f, 0.0f } };
    CHECK(connections(track) == one);

    // edits made on the grid reach the saved matrix
    while (!track.requestMatrixAttenuate(src, dest, false, -0.5f)) {}
    juce::var saved;
    track.getStateInformation(saved);
    auto text = juce::JSON::toString(saved.getProperty("matrix", juce::var()), true);
    CHECK(text == R"({"rows": ["1:1"], "cols": ["2:4"], "gain": [[0.5]]})");

    // a duplicate wire, then a toggle removes both
    while (!track.requestMatrixConnect(src, dest)) {}
    CHECK(connections(track).size() == 2);
    while (!track.requestMatrixToggle(src, dest)) {}
    CHECK(connections(track).empty());

    // a channel the module does not have adds nothing
    while (!track.requestMatrixToggle(Matrix::Jack(1, 2), dest)) {}
    CHECK(connections(track).empty());
}

static void trackGainIsTheConnection() {
    Track track;
    track.prepare(48000, 128);
    track.setStateInformation(juce::JSON::parse(R"({ "modules": { "1": "pass2", "2": "pass6" } })"), 0);
    Matrix::Jack src(1, 0), dest(2, 3);

    // stepping down from no wire adds nothing; stepping up adds one
    while (!track.requestMatrixGain(src, dest, -0.01f)) {}
    CHECK(connections(track).empty());
    while (!track.requestMatrixGain(src, dest, 0.01f)) {}
    std::vector<Conn> small = { { 1, 0, 2, 3, 0.01f, 0.0f } };
    CHECK(connections(track) == small);

    // clamped at 1, and 100 steps down land exactly on 0, which removes the wire
    while (!track.requestMatrixGain(src, dest, 5.0f)) {}
    std::vector<Conn> unity = { { 1, 0, 2, 3, 1.0f, 0.0f } };
    CHECK(connections(track) == unity);
    for (int i = 0; i < 99; i++) {
        while (!track.requestMatrixGain(src, dest, -0.01f)) {}
    }
    CHECK(connections(track).size() == 1);
    while (!track.requestMatrixGain(src, dest, -0.01f)) {}
    CHECK(connections(track).empty());

    // a channel the module does not have adds nothing
    while (!track.requestMatrixGain(Matrix::Jack(1, 2), dest, 0.5f)) {}
    CHECK(connections(track).empty());
}

static void trackSavesOnlyAudibleWires() {
    Track track;
    track.prepare(48000, 128);
    track.setStateInformation(juce::JSON::parse(R"({ "modules": { "1": "pass2", "2": "pass6" } })"), 0);
    while (!track.requestMatrixConnect(Matrix::Jack(1, 0), Matrix::Jack(2, 0), 0.5f)) {}
    while (!track.requestMatrixConnect(Matrix::Jack(1, 1), Matrix::Jack(2, 1), 0.0f, 0.2f)) {}

    // before the fix, the gain-0 wire's offset saved as dc with no wire, and reload rejected the matrix
    juce::var saved;
    track.getStateInformation(saved);
    Track reloaded;
    reloaded.prepare(48000, 128);
    reloaded.setStateInformation(saved, 0);
    std::vector<Conn> audible = { { 1, 0, 2, 0, 0.5f, 0.0f } };
    CHECK(connections(reloaded) == audible);

    // only muted wires: the save has no matrix
    Track muted;
    muted.prepare(48000, 128);
    muted.setStateInformation(juce::JSON::parse(R"({ "modules": { "1": "pass2", "2": "pass6" } })"), 0);
    while (!muted.requestMatrixConnect(Matrix::Jack(1, 0), Matrix::Jack(2, 0), 0.0f)) {}
    juce::var none;
    muted.getStateInformation(none);
    CHECK(none.getProperty("matrix", juce::var()).isVoid());
}

// every track's matrix in each preset of `dir` parses, as the SSP's loader parses it
static void shippedPresetsParse(const char* dir) {
    int seen = 0;
    for (auto& file : juce::File(dir).findChildFiles(juce::File::findFiles, false, "*.json")) {
        auto doc = juce::JSON::parse(file);
        CHECK(doc.isObject());
        if (auto* tracks = doc.getProperty("tracks", {}).getDynamicObject())
            for (auto& t : tracks->getProperties()) {
                juce::String error;
                auto wires = jsonpreset::parseMatrix(t.value.getProperty("matrix", {}), error);
                CHECK(error.isEmpty() && !wires.empty());
                if (error.isNotEmpty())
                    std::fprintf(stderr, "  %s track %s: %s\n", file.getFileName().toRawUTF8(),
                                 t.name.toString().toRawUTF8(), error.toRawUTF8());
            }
        seen++;
    }
    CHECK(seen > 0);
}

int main(int argc, char** argv) {
    if (argc > 1) shippedPresetsParse(argv[1]);
    parseExpandsCellsAndDc();
    parseRejectsMalformed();
    formatSumsAndOrders();
    formatDropsZeroCells();
    trackLoadsSavesAndReloads();
    trackTogglesWires();
    trackGainIsTheConnection();
    trackSavesOnlyAudibleWires();
    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
