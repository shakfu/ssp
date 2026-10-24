#include "LaunchpadView.h"

#include <algorithm>
#include <utility>

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

Led amber(int velocity) {  // a step that is on, brighter for higher velocity
    const float k = 0.2f + 0.8f * velocity / 127.0f;
    return Led::rgb(uint8_t(127 * k), uint8_t(80 * k), uint8_t(20 * k));
}
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

int LaunchpadView::pitchLow() const {
    std::lock_guard<std::mutex> l(lock_);
    return pitchLow_;
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
        if (!ev.down || e_.roll()) return;  // roll: unused
        if (ev.y >= KEY_ROWS) {
            const int r = top_ + 7 - ev.y;
            auto row = e_.row(r);
            row.mute = !row.mute;
            e_.setRow(r, row);
        } else {
            base_ = std::clamp(base_ + (ev.y == 1 ? 12 : -12), LOWEST, HIGHEST);
        }
    } else if (e_.roll()) {
        // a key pad held when roll mode came on still ends its note on release
        if (ev.y < KEY_ROWS && !ev.down) {
            int& held = held_[ev.y * WINDOW_STEPS + ev.x];
            if (held >= 0) e_.playIn(held, 0);
            held = -1;
        }
        const int col = left_ + ev.x;
        if (ev.down && col < e_.row(0).length) rollPad(pitchLow_ + ev.y, col, ev.velocity);
    } else if (ev.y < KEY_ROWS) {
        // A key's note-off is its own note, if the octave changed while it was held. Either entry point ends
        // it: the engine releases any held key by note.
        int& held = held_[ev.y * WINDOW_STEPS + ev.x];
        if (held >= 0) e_.playIn(held, 0);
        held = -1;
        if (!ev.down) return;
        const int n = keyNote(ev.x, ev.y, base_), v = ev.velocity ? ev.velocity : VELOCITY;
        if (n < 0) return;
        if (e_.record() != GseqEngine::IDLE)
            e_.noteIn(n, v);
        else
            e_.playIn(n, v);
        held = lastKey_ = n;
        heldVelocity_[ev.y * WINDOW_STEPS + ev.x] = ev.velocity;
    } else if (ev.down) {
        const int r = top_ + 7 - ev.y, col = left_ + ev.x;
        if (col < e_.row(r).length) cursorPad(r, col, ev.velocity);
    }
}

// Moves the cursor; in Step mode also writes: the held keys' notes, lowest on this row and the rest on the
// rows below, or with no key held, switches the step on (the last key's note, the pad's velocity) or off. A held
// key with velocity sets the step's; without, a step that was on keeps its own.
void LaunchpadView::cursorPad(int row, int col, int velocity) {
    e_.setCursor(row, col);
    cursorSeen_ = e_.cursorPitch() << 16 | row << 8 | col;
    if (!e_.stepEntry()) return;
    std::pair<int, int> notes[WINDOW_STEPS * KEY_ROWS];  // note, velocity
    int n = 0;
    for (int k = 0; k < WINDOW_STEPS * KEY_ROWS; k++)
        if (held_[k] >= 0) notes[n++] = { held_[k], heldVelocity_[k] };
    std::sort(notes, notes + n);
    const GseqEngine::Step fresh;
    if (n == 0) {
        auto s = e_.step(row, col);
        if (!s.on)
            s.note = uint8_t(lastKey_ >= 0 ? lastKey_ : fresh.note),
            s.velocity = uint8_t(velocity ? velocity : fresh.velocity), s.duration = fresh.duration;
        s.on = !s.on;
        e_.setStep(row, col, s);
        return;
    }
    for (int i = 0; i < n; i++) {
        const int r = (row + i) % GseqEngine::ROWS;
        if (col >= e_.row(r).length) continue;
        auto s = e_.step(r, col);
        if (!s.on) s.velocity = fresh.velocity, s.duration = fresh.duration;
        if (notes[i].second) s.velocity = uint8_t(notes[i].second);
        s.on = true;
        s.note = uint8_t(notes[i].first);
        e_.setStep(r, col, s);
    }
}

void LaunchpadView::button(int x) {
    switch (x) {
        case UP:  // roll: up a semitone
            if (e_.roll())
                pitchLow_ = std::min(HIGHEST_PITCH_LOW, pitchLow_ + 1);
            else
                top_ = std::max(0, top_ - 1);
            break;
        case DOWN:
            if (e_.roll())
                pitchLow_ = std::max(0, pitchLow_ - 1);
            else
                top_ = std::min(GseqEngine::ROWS - WINDOW_ROWS, top_ + 1);
            break;
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
    const int row = e_.cursorRow(), col = e_.cursorCol(), pitch = e_.cursorPitch();
    const int seen = pitch << 16 | row << 8 | col;
    if (seen != cursorSeen_) {
        cursorSeen_ = seen;
        if (row < top_) top_ = row;
        if (row >= top_ + WINDOW_ROWS) top_ = row - WINDOW_ROWS + 1;
        if (pitch < pitchLow_) pitchLow_ = pitch;
        if (pitch >= pitchLow_ + ROLL_ROWS) pitchLow_ = pitch - ROLL_ROWS + 1;
        if (col < left_ || col >= left_ + WINDOW_STEPS) left_ = col / WINDOW_STEPS * WINDOW_STEPS;
    }
    const int p = e_.playing(e_.roll() ? 0 : row);
    if (follow_ && p >= 0) left_ = p / WINDOW_STEPS * WINDOW_STEPS;
}

void LaunchpadView::render(Frame& f) {
    std::lock_guard<std::mutex> l(lock_);
    showCursor();
    f = Frame();
    if (e_.roll())
        renderRoll(f);
    else
        renderGrid(f);
    for (int y = 0; y < KEY_ROWS && !e_.roll(); y++)
        for (int x = 0; x < WINDOW_STEPS; x++) {
            const int n = keyNote(x, y, base_);
            if (n < 0) continue;
            const bool held = held_[y * WINDOW_STEPS + x] >= 0;
            f.at(x, y) = held                    ? WHITE
                         : (n - base_) % 12 == 0 ? Led::rgb(40, 40, 40)
                         : y == 0                ? Led::rgb(12, 12, 12)
                                                 : Led::rgb(0, 0, 24);
        }
    if (base_ < HIGHEST && !e_.roll()) f.at(SIDE, 1) = BUTTON;
    if (base_ > LOWEST && !e_.roll()) f.at(SIDE, 0) = BUTTON;
    if (e_.roll() ? pitchLow_ < HIGHEST_PITCH_LOW : top_ > 0) f.at(UP, TOP) = BUTTON;
    if (e_.roll() ? pitchLow_ > 0 : top_ < GseqEngine::ROWS - WINDOW_ROWS) f.at(DOWN, TOP) = BUTTON;
    if (left_ > 0) f.at(LEFT, TOP) = BUTTON;
    if (left_ < GseqEngine::STEPS - WINDOW_STEPS) f.at(RIGHT, TOP) = BUTTON;
    const auto rec = e_.record();
    f.at(REC, TOP) = rec == GseqEngine::ARMED       ? Led::flash(RED)
                     : rec == GseqEngine::RECORDING ? Led::palette(RED)
                                                    : Led::rgb(12, 0, 0);
    f.at(STEP, TOP) = e_.stepEntry() ? Led::palette(RED) : Led::rgb(12, 0, 0);
    f.at(FOLLOW, TOP) = follow_ ? Led::palette(TURQUOISE) : Led::rgb(0, 12, 12);
}

// grid mode's window: rows top_ .. top_ + 5, a voice each
void LaunchpadView::renderGrid(Frame& f) {
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
            } else if (s.on) {
                led = amber(s.velocity);
            } else {
                led = GREY;
            }
        }
        f.at(SIDE, y) = row.mute ? Led::palette(RED) : Led::rgb(8, 8, 8);
    }
}

// roll mode's window: every pad row, pitches pitchLow_ .. pitchLow_ + 7 upward, a note's start lit and the steps it
// holds dim
void LaunchpadView::renderRoll(Frame& f) {
    const auto track = e_.row(0);
    const int length = track.length, play = e_.playing(0), cp = e_.cursorPitch(), cc = e_.cursorCol();
    int start[ROLL_ROWS][WINDOW_STEPS] = {};  // velocity of a note starting there, 0 none
    bool hold[ROLL_ROWS][WINDOW_STEPS] = {};
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        const int len = e_.row(r).length;
        for (int s = 0; s < len; s++) {
            const auto st = e_.step(r, s);
            const int w = st.note - pitchLow_;
            if (!st.on || w < 0 || w >= ROLL_ROWS) continue;
            const int steps = std::max(1, std::min<int>(GseqEngine::DURATION_EIGHTHS[st.duration], len * 8) / 8);
            for (int t = 0; t < steps; t++) {
                const int x = (s + t) % len - left_;
                if (x < 0 || x >= WINDOW_STEPS) continue;
                if (t == 0)
                    start[w][x] = st.velocity;
                else
                    hold[w][x] = true;
            }
        }
    }
    for (int w = 0; w < ROLL_ROWS; w++) {
        const int pitch = pitchLow_ + w, y = w;
        for (int x = 0; x < WINDOW_STEPS; x++) {
            const int col = left_ + x;
            if (col >= length) continue;
            Led& led = f.at(x, y);
            if (col == play)
                led = start[w][x] ? WHITE : Led::rgb(20, 20, 20);
            else if (pitch == cp && col == cc)
                led = Led::flash(TURQUOISE, start[w][x] ? ORANGE : 0);
            else if (start[w][x])
                led = amber(start[w][x]);
            else if (hold[w][x])
                led = Led::rgb(30, 18, 4);
            else
                led = pitch % 12 == 0 ? Led::rgb(10, 10, 10) : GREY;
        }
    }
}

// Step mode adds or removes the note at the pad, a new one with the pad's velocity; any mode moves the cursor there
void LaunchpadView::rollPad(int pitch, int col, int velocity) {
    e_.setCursor(e_.cursorRow(), col);
    e_.setCursorPitch(pitch);
    cursorSeen_ = pitch << 16 | e_.cursorRow() << 8 | col;
    if (!e_.stepEntry() || e_.removeNote(col, pitch) >= 0) return;
    GseqEngine::Step st;
    st.note = uint8_t(pitch);
    if (velocity) st.velocity = uint8_t(velocity);
    e_.addNote(col, st);  // refused when every row sounds at the step
}

}  // namespace gseq
