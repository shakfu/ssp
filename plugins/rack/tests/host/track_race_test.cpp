// Drives Track from an audio thread and a control thread at once, for ThreadSanitizer.
// Built by CMakeLists.txt and run from a directory holding plugins/pass2.so and plugins/pass6.so.

#include <atomic>
#include <cstdio>
#include <thread>

#include "Track.h"

static int failures = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static constexpr unsigned SLOT = Track::M_SLOT_1;

static void patch(Track& track, const char* module) {
    while (!track.requestModuleChange(SLOT, module)) {}
    while (!track.requestMatrixConnect(Matrix::Jack(Track::M_IN, 0), Matrix::Jack(SLOT, 0))) {}
    while (!track.requestMatrixConnect(Matrix::Jack(SLOT, 0), Matrix::Jack(Track::M_OUT, 0))) {}
    while (!track.requestMatrixConnect(Matrix::Jack(SLOT, 1), Matrix::Jack(SLOT + 1, 0))) {}
}

int main() {
    Track track;
    track.prepare(48000, 128);

    // sanity: the plugins load, and the wires are accepted
    patch(track, "pass2");
    CHECK(track.modules_[SLOT].plugin_ != nullptr);
    CHECK(track.connections().size() == 2);  // SLOT + 1 is empty, so its wire is refused

    std::atomic<bool> stop{ false };
    std::thread audio([&] {
        juce::AudioSampleBuffer io(8, 128);
        while (!stop) {
            io.clear();
            track.process(io);
        }
    });

    for (int i = 0; i < 200; i++) {
        patch(track, i % 2 ? "pass2" : "pass6");
        while (!track.requestMatrixAttenuate(Matrix::Jack(Track::M_IN, 0), Matrix::Jack(SLOT, 0), false, 0.01f)) {}
        while (!track.requestMatrixDisconnect(Matrix::Jack(SLOT, 0), Matrix::Jack(Track::M_OUT, 0))) {}

        if (i % 4 == 0) {
            juce::XmlElement xml("Track");
            track.getStateInformation(xml);
            track.setStateInformation(xml);
        } else if (i % 4 == 1) {
            juce::var json;
            track.getStateInformation(json);
            while (!track.requestClearTrack()) {}
            track.setStateInformation(json, 0);
        } else if (i % 4 == 2) {
            while (!track.requestClearTrack()) {}
            track.mute(i % 8 == 2);
            track.level(0.5f);
        }
    }

    stop = true;
    audio.join();

    if (failures) std::fprintf(stderr, "%d failures\n", failures);
    return failures ? 1 : 0;
}
