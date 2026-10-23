#include "LaunchpadView.h"

#include <algorithm>

namespace gseq {

using ssp::launchpad::Event;
using ssp::launchpad::Frame;
using ssp::launchpad::Led;

namespace {
constexpr int TOP = 8, SIDE = 8;  // the top row's y, the side keys' x
enum Button {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    REC,
    STEP,
    FOLLOW
};
// palette entries (the manual's colour table)
constexpr uint8_t RED = 5, ORANGE = 9, TURQUOISE = 37;

const Led WHITE = Led::rgb(127, 127, 127), GREY = Led::rgb(4, 4, 4), BUTTON = Led::rgb(16, 16, 16);
}  // namespace

int LaunchpadView::keyNote(int x, int y, int base) {
    static const int white[8] = { 0, 2, 4, 5, 7, 9, 11, 12 }, black[8] = { -1, 1, 3, -1, 6, 8, 10, -1 };
    if (x < 0 || x >= 8 || y < 0 || y >= KEY_ROWS) return -1;
    const int k = y == 0 ? white[x] : black[x];
    return k < 0 ? -1 : base + k;
}

int LaunchpadView::top() const {
    std::lock_guard<std::mutex> l(lock_);
    return top_;
}

int LaunchpadView::left() const {
    std::lock_guard<std::mutex> l(lock_);
    return left_;
}

int LaunchpadView::octave() const {
    std::lock_guard<std::mutex> l(lock_);
    return base_;
}

bool LaunchpadView::follow() const {
    std::lock_guard<std::mutex> l(lock_);
    return follow_;
}

void LaunchpadView::press(const Event& ev) {
    std::lock_guard<std::mutex> l(lock_);
    if (ev.y == TOP) {
        if (ev.down && ev.x < SIDE) button(ev.x);
    } else if (ev.x == SIDE) {
        if (!ev.down) return;
        if (ev.y >= KEY_ROWS) {
            const int r = top_ + 7 - ev.y;
            auto row = e_.row(r);
            row.mute = !row.mute;
            e_.setRow(r, row);
        } else {
            base_ = std::clamp(base_ + (ev.y == 1 ? 12 : -12), LOWEST, HIGHEST);
        }
    } else if (ev.y < KEY_ROWS) {
        // A key's note-off is its own note, if the octave changed while it was held. Either entry point ends
        // it: the engine releases any held key by note.
        int& held = held_[ev.y * WINDOW_STEPS + ev.x];
        if (held >= 0) e_.playIn(held, 0);
        held = -1;
        if (!ev.down) return;
        const int n = keyNote(ev.x, ev.y, base_);
        if (n < 0) return;
        if (e_.record() != GseqEngine::IDLE)
            e_.noteIn(n, VELOCITY);
        else
            e_.playIn(n, VELOCITY);
        held = lastKey_ = n;
    } else if (ev.down) {
        const int r = top_ + 7 - ev.y, col = left_ + ev.x;
        if (col < e_.row(r).length) cursorPad(r, col);
    }
}

// Moves the cursor; in Step mode also writes: the held keys' notes, lowest on this row and the rest on the
// rows below, or with no key held, switches the step on (the last key's note) or off.
void LaunchpadView::cursorPad(int row, int col) {
    e_.setCursor(row, col);
    cursorSeen_ = row << 8 | col;
    if (!e_.stepEntry()) return;
    int notes[WINDOW_STEPS * KEY_ROWS], n = 0;
    for (int h : held_)
        if (h >= 0) notes[n++] = h;
    std::sort(notes, notes + n);
    const GseqEngine::Step fresh;
    if (n == 0) {
        auto s = e_.step(row, col);
        if (!s.on)
            s.note = uint8_t(lastKey_ >= 0 ? lastKey_ : fresh.note), s.velocity = fresh.velocity,
            s.duration = fresh.duration;
        s.on = !s.on;
        e_.setStep(row, col, s);
        return;
    }
    for (int i = 0; i < n; i++) {
        const int r = (row + i) % GseqEngine::ROWS;
        if (col >= e_.row(r).length) continue;
        auto s = e_.step(r, col);
        if (!s.on) s.velocity = fresh.velocity, s.duration = fresh.duration;
        s.on = true;
        s.note = uint8_t(notes[i]);
        e_.setStep(r, col, s);
    }
}

void LaunchpadView::button(int x) {
    switch (x) {
        case UP: top_ = std::max(0, top_ - 1); break;
        case DOWN: top_ = std::min(GseqEngine::ROWS - WINDOW_ROWS, top_ + 1); break;
        case LEFT: left_ = std::max(0, left_ - WINDOW_STEPS); break;
        case RIGHT: left_ = std::min(GseqEngine::STEPS - WINDOW_STEPS, left_ + WINDOW_STEPS); break;
        case REC:  // as the SSP's Rec: arms the cursor's row; again, stops the take
            e_.setStepEntry(false);
            e_.toggleRecord();
            break;
        case STEP: e_.setStepEntry(!e_.stepEntry()); break;
        case FOLLOW: follow_ = !follow_; break;
        default: break;
    }
}

// The window moves the least it must, by rows and by pages of 8 steps, to show a cursor that moved since
// the last frame; the arrows can then move it away again. Follow pages to the cursor row's playhead.
void LaunchpadView::showCursor() {
    const int row = e_.cursorRow(), col = e_.cursorCol(), seen = row << 8 | col;
    if (seen != cursorSeen_) {
        cursorSeen_ = seen;
        if (row < top_) top_ = row;
        if (row >= top_ + WINDOW_ROWS) top_ = row - WINDOW_ROWS + 1;
        if (col < left_ || col >= left_ + WINDOW_STEPS) left_ = col / WINDOW_STEPS * WINDOW_STEPS;
    }
    const int p = e_.playing(row);
    if (follow_ && p >= 0) left_ = p / WINDOW_STEPS * WINDOW_STEPS;
}

void LaunchpadView::render(Frame& f) {
    std::lock_guard<std::mutex> l(lock_);
    showCursor();
    f = Frame();
    const int cr = e_.cursorRow(), cc = e_.cursorCol();
    for (int w = 0; w < WINDOW_ROWS; w++) {
        const int r = top_ + w, y = 7 - w;
        const auto row = e_.row(r);
        const int play = e_.playing(r);
        const bool out = row.channel || row.voice;
        for (int x = 0; x < WINDOW_STEPS; x++) {
            const int col = left_ + x;
            if (col >= row.length) continue;
            const auto s = e_.step(r, col);
            Led& led = f.at(x, y);
            if (col == play) {
                led = WHITE;
            } else if (r == cr && col == cc) {
                led = Led::flash(TURQUOISE, s.on ? ORANGE : 0);
            } else if (s.on && !out) {
                led = Led::rgb(24, 24, 24);
            } else if (s.on) {  // amber, brighter for higher velocity
                const float k = 0.2f + 0.8f * s.velocity / 127.0f;
                led = Led::rgb(uint8_t(127 * k), uint8_t(80 * k), uint8_t(20 * k));
            } else {
                led = GREY;
            }
        }
        f.at(SIDE, y) = row.mute ? Led::palette(RED) : Led::rgb(8, 8, 8);
    }
    for (int y = 0; y < KEY_ROWS; y++)
        for (int x = 0; x < WINDOW_STEPS; x++) {
            const int n = keyNote(x, y, base_);
            if (n < 0) continue;
            const bool held = held_[y * WINDOW_STEPS + x] >= 0;
            f.at(x, y) = held                    ? WHITE
                         : (n - base_) % 12 == 0 ? Led::rgb(40, 40, 40)
                         : y == 0                ? Led::rgb(12, 12, 12)
                                                 : Led::rgb(0, 0, 24);
        }
    if (base_ < HIGHEST) f.at(SIDE, 1) = BUTTON;
    if (base_ > LOWEST) f.at(SIDE, 0) = BUTTON;
    if (top_ > 0) f.at(UP, TOP) = BUTTON;
    if (top_ < GseqEngine::ROWS - WINDOW_ROWS) f.at(DOWN, TOP) = BUTTON;
    if (left_ > 0) f.at(LEFT, TOP) = BUTTON;
    if (left_ < GseqEngine::STEPS - WINDOW_STEPS) f.at(RIGHT, TOP) = BUTTON;
    const auto rec = e_.record();
    f.at(REC, TOP) = rec == GseqEngine::ARMED       ? Led::flash(RED)
                     : rec == GseqEngine::RECORDING ? Led::palette(RED)
                                                    : Led::rgb(12, 0, 0);
    f.at(STEP, TOP) = e_.stepEntry() ? Led::palette(RED) : Led::rgb(12, 0, 0);
    f.at(FOLLOW, TOP) = follow_ ? Led::palette(TURQUOISE) : Led::rgb(0, 12, 12);
}

}  // namespace gseq
