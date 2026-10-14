// Step 4 of docs/dev/sapf-plugin.md, on the host: the editor's TextBuffer and KeyQueue, and the Session
// the plugin runs, with a thread standing in for the audio callback.
//
// usage: sapf_session_test PRELUDE UGENS OUTDIR

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "FilePicker.h"
#include "KeyQueue.h"
#include "Session.h"
#include "TextBuffer.h"
#include "Texture.hpp"

namespace {

int failures = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);  \
            failures++;                                            \
        }                                                          \
    } while (0)

using sapf::TextBuffer;
constexpr uint8_t ENTER = 0x28, BACKSPACE = 0x2A, LEFT = 0x50, UP = 0x52, DOWN = 0x51, ESC = 0x29, DOT = 0x37,
                  LSHIFT = 0xE1, LCTRL = 0xE0;

void type(TextBuffer& b, const std::string& s) {
    for (char c : s) {
        for (int u = 0x04; u < 0x39; u++) {
            bool done = false;
            for (bool shift : { false, true }) {
                if (TextBuffer::character(uint8_t(u), shift) == c) {
                    b.key(uint8_t(u), shift ? TextBuffer::SHIFT : 0);
                    done = true;
                    break;
                }
            }
            if (done) break;
        }
    }
}

void testTextBuffer() {
    TextBuffer b;
    type(b, "440 0 sinosc");
    CHECK(b.text() == "440 0 sinosc");
    b.key(BACKSPACE, 0);
    CHECK(b.text() == "440 0 sinos");
    type(b, "c \"Hi\" [1]");
    CHECK(b.text() == "440 0 sinosc \"Hi\" [1]");

    // Enter keeps the indent; Backspace at a line's start joins it to the one above
    b.setText("  a");
    b.key(0x4D, 0);  // End
    b.key(ENTER, 0);
    CHECK(b.lines().size() == 2 && b.lines()[1] == "  " && b.col() == 2);
    b.key(BACKSPACE, 0), b.key(BACKSPACE, 0), b.key(BACKSPACE, 0);
    CHECK(b.text() == "  a" && b.row() == 0 && b.col() == 3);

    // up and down keep the column across a short line
    b.setText("abcdef\nx\nabcdef");
    for (int i = 0; i < 5; i++) b.key(0x4F, 0);  // Right
    b.key(DOWN, 0);
    CHECK(b.row() == 1 && b.col() == 1);
    b.key(DOWN, 0);
    CHECK(b.row() == 2 && b.col() == 5);
    b.key(LEFT, 0), b.key(UP, 0), b.key(UP, 0);
    CHECK(b.row() == 0 && b.col() == 4);

    // tabs in a file become spaces
    b.setText("\tx");
    CHECK(b.lines()[0] == "    x");

    // blocks: a blank line inside brackets stays in its block; on a blank line between blocks, the
    // block above
    b.setText("1 2 +\n\n[440\n\n550] 0 sinosc\n\n\n; a comment [\n\n3");
    int first = -1, last = -1;
    CHECK(b.block(first, last) && first == 0 && last == 0);
    b.key(DOWN, 0);
    CHECK(b.block(first, last) && first == 0 && last == 0);
    b.key(DOWN, 0), b.key(DOWN, 0);
    CHECK(b.block(first, last) && first == 2 && last == 4);
    CHECK(b.lines(first, last) == "[440\n\n550] 0 sinosc");
    for (int i = 0; i < 5; i++) b.key(DOWN, 0);
    CHECK(b.row() == 8 && b.block(first, last) && first == 7 && last == 7);  // the bracket is in a comment
    b.key(DOWN, 0);
    CHECK(b.row() == 9 && b.block(first, last) && first == 9 && last == 9);
    b.setText("\n\nx");
    CHECK(!b.block(first, last));

    // actions
    CHECK(b.key(ENTER, TextBuffer::CTRL) == TextBuffer::EVALUATE);
    CHECK(b.key(DOT, TextBuffer::CTRL) == TextBuffer::STOP);
    CHECK(b.key(ESC, 0) == TextBuffer::STOP);
    CHECK(b.key(0x16, TextBuffer::CTRL) == TextBuffer::SAVE);
    CHECK(b.text() == "\n\nx");  // chords type nothing
    std::printf("text buffer: done\n");
}

void testKeyQueue() {
    using Clock = sapf::KeyQueue::Clock;
    sapf::KeyQueue q;
    q.keyDown(LSHIFT);
    q.keyDown(0x04);  // a
    q.keyUp(0x04);
    q.keyUp(LSHIFT);
    q.keyDown(0x05);  // b
    q.keyUp(0x05);
    auto keys = q.take(Clock::now());
    CHECK(keys.size() == 2 && keys[0].usage == 0x04 && keys[0].mods == TextBuffer::SHIFT && keys[1].mods == 0);

    // a held key repeats after DELAY, every RATE
    auto down = Clock::now();
    q.keyDown(0x06);
    CHECK(q.take(down + std::chrono::milliseconds(100)).size() == 1);
    keys = q.take(down + std::chrono::milliseconds(650));
    CHECK(keys.size() >= 5 && keys.size() <= 6);  // 400, 450, ... 650 ms
    q.keyUp(0x06);
    CHECK(q.take(down + std::chrono::milliseconds(900)).empty());

    // a held chord acts once
    q.keyDown(LCTRL);
    q.keyDown(ENTER);
    keys = q.take(Clock::now() + std::chrono::seconds(1));
    CHECK(keys.size() == 1 && keys[0].mods == TextBuffer::CTRL);
    q.releaseAll();
    q.keyDown(0x04);
    CHECK(q.take(Clock::now()).at(0).mods == 0);  // releaseAll dropped the held Ctrl
    q.keyUp(0x04);

    // TCP taps carry a HID modifier byte: 0x20 is right Shift
    q.keyTap(0x1E, 0x20);
    keys = q.take(Clock::now());
    CHECK(keys.size() == 1 && TextBuffer::character(keys[0].usage, keys[0].mods & TextBuffer::SHIFT) == '!');
    std::printf("key queue: done\n");
}

void testFilePicker() {
    sapf::FilePicker p;
    p.show({ "a.sapf", "live.sapf", "sapf-examples.txt" }, "live.sapf");
    CHECK(p.shown() && p.selected() == 1);  // the current file
    CHECK(p.key(0x51) == sapf::FilePicker::NONE && p.selected() == 2);  // Down
    p.key(0x51);
    CHECK(p.selected() == 2);  // clamped
    p.key(0x4A);               // Home
    CHECK(p.selected() == 0);
    CHECK(p.key(0x28) == sapf::FilePicker::OPEN);    // Enter
    CHECK(p.key(0x29) == sapf::FilePicker::CANCEL);  // Esc
    p.show({}, "live.sapf");
    CHECK(p.selected() == 0 && p.key(0x28) == sapf::FilePicker::CANCEL);  // nothing to open
    std::printf("file picker: done\n");
}

bool waitFor(std::function<bool()> cond, double seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

std::string outputText(const sapf::Session& s) {
    std::string t;
    for (auto& l : s.output(1000)) t += l.text + "\n";
    return t;
}

bool idle(const sapf::Session& s) {
    return s.state() == sapf::Session::State::IDLE;
}

void testSession(const char* prelude, const char* ugens, const char* outdir) {
    sapf::Session s({ ugens, prelude, outdir });
    s.prepare(48000.f);
    CHECK(waitFor([&] { return idle(s); }, 30));

    // the audio callback
    std::atomic<bool> running{ true };
    std::atomic<float> peak{ 0.f };
    std::thread audio([&] {
        constexpr int N = 256;
        static float in[sapf::Session::CHANNELS][N] = {}, out[sapf::Session::CHANNELS][N];
        const float* ins[sapf::Session::CHANNELS];
        float* outs[sapf::Session::CHANNELS];
        for (int c = 0; c < sapf::Session::CHANNELS; c++) ins[c] = in[c], outs[c] = out[c];
        auto next = std::chrono::steady_clock::now();
        while (running) {
            s.process(ins, outs, N);
            float p = 0.f;
            for (float x : out[0]) p = std::max(p, std::fabs(x));
            peak = p;
            next += std::chrono::microseconds(int64_t(1e6 * N / 48000));
            std::this_thread::sleep_until(next);
        }
    });

    // results and errors reach the output
    CHECK(s.evaluate("2 3 +"));
    CHECK(waitFor([&] { return outputText(s).find("stack: 5") != std::string::npos && idle(s); }, 5));
    CHECK(s.evaluate("1 2 ]"));
    CHECK(waitFor([&] { return outputText(s).find("error") != std::string::npos && idle(s); }, 5));
    CHECK(!s.output(1000).empty() && s.output(1000).back().error);

    // a definition persists into the next evaluation
    CHECK(s.evaluate("7 = seven"));
    CHECK(waitFor([&] { return idle(s); }, 5));
    CHECK(s.evaluate("seven 1 +"));
    CHECK(waitFor([&] { return outputText(s).find("stack: 8") != std::string::npos; }, 5));

    // Stop interrupts a computation that would run for hours, and refuses nothing after
    CHECK(waitFor([&] { return idle(s); }, 5));
    CHECK(s.evaluate("ord 1e12 N +/"));
    CHECK(waitFor([&] { return outputText(s).find("> ord 1e12") != std::string::npos; }, 5));  // started
    CHECK(!s.evaluate("1"));  // busy
    s.stop();
    CHECK(waitFor([&] { return outputText(s).find("stopped") != std::string::npos && idle(s); }, 3));
    CHECK(outputText(s).find("error: interrupted") != std::string::npos);

    // the watchdog interrupts an evaluation that keeps allocating
    CHECK(s.evaluate("ord 1e9 N reverse"));
    CHECK(waitFor([&] { return outputText(s).find("memory grew") != std::string::npos && idle(s); }, 30));

    // sound, then silence after stop
    CHECK(s.evaluate("440 0 sinosc .5 * play"));
    CHECK(waitFor([&] { return peak > 0.4f; }, 5));
    s.stop();
    CHECK(waitFor([&] { return peak == 0.f; }, 3));

    // a second Session in the process stays silent
    {
        sapf::Session second({ ugens, prelude, outdir });
        second.prepare(48000.f);
        CHECK(second.state() == sapf::Session::State::SECOND && !second.evaluate("1"));
    }

    // teardown with ola voices playing: their threads end first
    CHECK(waitFor([&] { return idle(s); }, 5));
    CHECK(s.evaluate("\\[0 0 impulse 64 N] .1 .2 randz 1 1 ola play"));
    CHECK(waitFor([&] { return runningTextures() > 0; }, 5));
    running = false;
    audio.join();
    std::printf("session: done\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    testTextBuffer();
    testKeyQueue();
    testFilePicker();
    testSession(argv[1], argv[2], argv[3]);  // the Session's destructor runs here
    CHECK(runningTextures() == 0);
    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
