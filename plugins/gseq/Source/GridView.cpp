#include "GridView.h"

#include <algorithm>

namespace gseq {

using ssp::monome::Frame;
using ssp::monome::Key;

namespace {
// the cursor, in its lit phase: full, or dark over a key already near full
uint8_t blinked(uint8_t level) {
    return level >= GridView::ON_HIGH ? 0 : 15;
}
}  // namespace

int GridView::left() const {
    std::lock_guard<std::mutex> l(lock_);
    return left_;
}

int GridView::pitchLow() const {
    std::lock_guard<std::mutex> l(lock_);
    return pitchLow_;
}

void GridView::press(const Key& k) {
    if (!k.down || k.x < 0 || k.x >= STEPS || k.y < 0 || k.y >= PITCHES) return;
    std::lock_guard<std::mutex> l(lock_);
    const int col = left_ + k.x;
    if (e_.roll()) {
        if (col >= e_.row(0).length) return;
        const int pitch = pitchLow_ + PITCHES - 1 - k.y;
        e_.setCursor(e_.cursorRow(), col);
        e_.setCursorPitch(pitch);
        cursorSeen_ = pitch << 16 | e_.cursorRow() << 8 | col;
        if (e_.removeNote(col, pitch) >= 0) return;
        GseqEngine::Step st;
        st.note = uint8_t(pitch);
        e_.addNote(col, st);  // refused when every row sounds at the step
        return;
    }
    const int r = k.y;
    if (col >= e_.row(r).length) return;
    e_.setCursor(r, col);
    cursorSeen_ = e_.cursorPitch() << 16 | r << 8 | col;
    auto s = e_.step(r, col);
    if (!s.on) {
        const GseqEngine::Step fresh;
        const int last = e_.lastNote();
        s.note = uint8_t(last >= 0 ? last : fresh.note);
        s.velocity = fresh.velocity;
        s.duration = fresh.duration;
    }
    s.on = !s.on;
    e_.setStep(r, col, s);
}

// The page and roll's pitches move the least they must to show a cursor that moved since the last frame.
void GridView::showCursor() {
    const int row = e_.cursorRow(), col = e_.cursorCol(), pitch = e_.cursorPitch();
    const int seen = pitch << 16 | row << 8 | col;
    if (seen == cursorSeen_) return;
    cursorSeen_ = seen;
    left_ = col / STEPS * STEPS;
    if (pitch < pitchLow_) pitchLow_ = pitch;
    if (pitch >= pitchLow_ + PITCHES) pitchLow_ = std::min(HIGHEST_PITCH_LOW, pitch - PITCHES + 1);
}

void GridView::render(Frame& f, bool blink) {
    std::lock_guard<std::mutex> l(lock_);
    showCursor();
    f = Frame();
    if (e_.roll())
        renderRoll(f, blink);
    else
        renderGrid(f, blink);
}

void GridView::renderGrid(Frame& f, bool blink) {
    const int cr = e_.cursorRow(), cc = e_.cursorCol();
    for (int y = 0; y < PITCHES; y++) {
        const auto row = e_.row(y);
        const int play = e_.playing(y);
        const bool sounds = (row.channel || row.voice) && !row.mute;
        for (int x = 0; x < STEPS; x++) {
            const int col = left_ + x;
            if (col >= row.length) continue;
            const auto s = e_.step(y, col);
            uint8_t level = !s.on ? OFF : sounds ? onLevel(s.velocity) : SILENT;
            if (col == play) level = s.on ? PLAY_ON : PLAY_OFF;
            if (y == cr && col == cc && blink) level = blinked(level);
            f.at(x, y) = level;
        }
    }
}

// a note's start lit by its velocity, the steps it holds dim, as LaunchpadView::renderRoll
void GridView::renderRoll(Frame& f, bool blink) {
    const int length = e_.row(0).length, play = e_.playing(0), cp = e_.cursorPitch(), cc = e_.cursorCol();
    int start[PITCHES][STEPS] = {};  // velocity of a note starting there, 0 none
    bool hold[PITCHES][STEPS] = {};
    for (int r = 0; r < GseqEngine::ROWS; r++) {
        const int len = e_.row(r).length;
        for (int s = 0; s < len; s++) {
            const auto st = e_.step(r, s);
            const int w = st.note - pitchLow_;
            if (!st.on || w < 0 || w >= PITCHES) continue;
            const int steps = std::max(1, std::min<int>(GseqEngine::DURATION_EIGHTHS[st.duration], len * 8) / 8);
            for (int t = 0; t < steps; t++) {
                const int x = (s + t) % len - left_;
                if (x < 0 || x >= STEPS) continue;
                if (t == 0)
                    start[w][x] = st.velocity;
                else
                    hold[w][x] = true;
            }
        }
    }
    for (int w = 0; w < PITCHES; w++) {
        const int pitch = pitchLow_ + w, y = PITCHES - 1 - w;
        for (int x = 0; x < STEPS; x++) {
            const int col = left_ + x;
            if (col >= length) continue;
            uint8_t level = start[w][x] ? onLevel(start[w][x]) : hold[w][x] ? HOLD : pitch % 12 == 0 ? C_ROW : PITCH_ROW;
            if (col == play) level = start[w][x] ? PLAY_ON : PLAY_OFF;
            if (pitch == cp && col == cc && blink) level = blinked(level);
            f.at(x, y) = level;
        }
    }
}

}  // namespace gseq
