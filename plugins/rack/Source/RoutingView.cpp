#include "RoutingView.h"

#include <algorithm>

#include "GraphOrder.h"

static constexpr int FH = 10 * COMPACT_UI_SCALE;
static constexpr int SMALL_FH = 7 * COMPACT_UI_SCALE;  // headers: a 4-letter module name in one cell

static juce::Font monoFont(int fh) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), float(fh), juce::Font::plain));
}

static juce::Colour kindColour(rack::WireKind kind) {
    switch (kind) {
        case rack::WireKind::Forward: return juce::Colours::lightgreen;
        case rack::WireKind::Delayed: return juce::Colours::orange;
        case rack::WireKind::Self: return juce::Colours::red;
    }
    return juce::Colours::white;
}

RoutingView::RoutingView(PluginProcessor& p) : base_type(&p, nullptr), processor_(p) {
    levelBtn_ = std::make_shared<ssp::ValueButton>("Level", [this](bool v) { levelMode_ = v; });
    levelBtn_->setToggle(true);
    addButton(0, levelBtn_);
}

// wires and order are rebuilt each frame from the track, so the view never shows stale routing
void RoutingView::refresh() {
    wires_ = processor_.track(trackIdx_).connections();

    bool adj[N][N] = {};
    for (auto& row : count_) {
        for (auto& c : row) c = 0;
    }
    for (auto& w : wires_) {
        if (w.src_.modIdx_ >= N || w.dest_.modIdx_ >= N) continue;
        adj[w.src_.modIdx_][w.dest_.modIdx_] = true;
        count_[w.src_.modIdx_][w.dest_.modIdx_]++;
    }
    order_ = rack::executionOrder(adj);

    // an empty slot has no jacks, so it has no row or column
    auto& modules = processor_.track(trackIdx_).modules_;
    unsigned r = 0, c = 0;
    for (auto m : order_) {
        if (m != Track::M_IN && m != Track::M_OUT && modules[m].pluginName_.empty()) continue;
        if (m != Track::M_OUT) rows_[r++] = m;
        if (m != Track::M_IN) cols_[c++] = m;
    }
    nRows_ = r;
    nCols_ = c;
    row_ = std::min(row_, nRows_ - 1);  // IN and OUT are always there, so neither count is 0
    col_ = std::min(col_, nCols_ - 1);
}

void RoutingView::resized() {
    base_type::resized();
    showButtonBox(!wide());
}

void RoutingView::editorShown() {
    base_type::editorShown();
    levelBtn_->value(false);
    refresh();
    // start on the first wired cell, so the detail pane shows something
    row_ = col_ = 0;
    for (unsigned r = 0; r < nRows_ && !count_[rows_[row_]][cols_[col_]]; r++) {
        for (unsigned c = 0; c < nCols_; c++) {
            if (count_[rows_[r]][cols_[c]] > 0) {
                row_ = r;
                col_ = c;
                break;
            }
        }
    }
    resetCursor();
}

// put the jack cursor on the cell's first wire, or its first jacks if it has none
void RoutingView::resetCursor() {
    srcCh_ = destCh_ = 0;
    for (auto& w : wires_) {
        if (w.src_.modIdx_ == rows_[row_] && w.dest_.modIdx_ == cols_[col_]) {
            srcCh_ = w.src_.chIdx_;
            destCh_ = w.dest_.chIdx_;
            return;
        }
    }
}

void RoutingView::onEncoder(unsigned enc, float v) {
    int step = v > 0 ? 1 : -1;
    auto move = [step](unsigned& pos, unsigned limit) {
        int next = int(pos) + step;
        if (next >= 0 && next < int(limit)) pos = unsigned(next);
    };
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    auto& track = processor_.track(trackIdx_);
    Matrix::Jack srcJack(src, srcCh_), destJack(dest, destCh_);
    // offset and gain: 0.1 a detent, 0.01 while the encoder is held
    if (enc < 4 && encDown_[enc]) encTurned_[enc] = true;
    float level = v * (enc < 4 && encDown_[enc] ? 0.01f : 0.1f);
    switch (enc) {
        case 0:
            move(row_, nRows_);
            refresh();
            resetCursor();
            break;
        case 1:
            move(col_, nCols_);
            refresh();
            resetCursor();
            break;
        case 2:
            if (levelMode_) {
                while (!track.requestMatrixAttenuate(srcJack, destJack, true, level)) {}
            } else {
                move(srcCh_, channelCount(src, true));
            }
            break;
        case 3:
            if (levelMode_) {
                if (src != dest) {
                    while (!track.requestMatrixGain(srcJack, destJack, level)) {}
                }
            } else {
                move(destCh_, channelCount(dest, false));
            }
            break;
        default: break;
    }
}

// a press of encoder 4 sets the cursor wire's gain to 1, or to 0, which removes it. It acts on
// release, and not when the encoder was turned while held, which is a fine adjustment.
void RoutingView::onEncoderSwitch(unsigned enc, bool v) {
    if (enc >= 4) return;
    if (v) {
        encDown_[enc] = true;
        encTurned_[enc] = false;
        return;
    }
    encDown_[enc] = false;
    if (enc == 3 && !encTurned_[enc]) toggleWire();
}

void RoutingView::toggleWire() {
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    // a self-wire carries nothing, and the cursor may sit past a module's last jack
    if (src == dest || srcCh_ >= channelCount(src, true) || destCh_ >= channelCount(dest, false)) return;
    auto& track = processor_.track(trackIdx_);
    while (!track.requestMatrixToggle(Matrix::Jack(src, srcCh_), Matrix::Jack(dest, destCh_))) {}
}

bool RoutingView::isDuplicate(unsigned midx) const {
    auto& modules = processor_.track(trackIdx_).modules_;
    auto& name = modules[midx].pluginName_;
    if (name.empty() || midx == Track::M_IN || midx == Track::M_OUT) return false;
    for (unsigned m = Track::M_SLOT_1; m < Track::M_OUT; m++) {
        if (m != midx && modules[m].pluginName_ == name) return true;
    }
    return false;
}

// module name; an empty slot shows its number, and a repeated module adds its slot on a second line
juce::String RoutingView::headerLabel(unsigned midx) const {
    if (midx == Track::M_IN) return "IN";
    if (midx == Track::M_OUT) return "OUT";
    auto slot = juce::String(midx - Track::M_SLOT_1 + 1);
    auto& name = processor_.track(trackIdx_).modules_[midx].pluginName_;
    if (name.empty()) return slot;
    return juce::String(name) + (isDuplicate(midx) ? "\n" + slot : "");
}

juce::String RoutingView::moduleName(unsigned midx) const {
    if (midx == Track::M_IN) return "track in";
    if (midx == Track::M_OUT) return "track out";
    auto slot = juce::String(midx - Track::M_SLOT_1 + 1);
    auto& name = processor_.track(trackIdx_).modules_[midx].pluginName_;
    if (name.empty()) return "empty slot " + slot;
    return juce::String(name) + (isDuplicate(midx) ? " #" + slot : "");
}

unsigned RoutingView::channelCount(unsigned midx, bool output) {
    auto& module = processor_.track(trackIdx_).modules_[midx];
    if (!module.descriptor_) return 0;
    return unsigned(output ? module.descriptor_->outputChannelNames.size() : module.descriptor_->inputChannelNames.size());
}

juce::String RoutingView::channelName(unsigned midx, unsigned ch, bool output) {
    auto& module = processor_.track(trackIdx_).modules_[midx];
    if (module.descriptor_) {
        auto& names = output ? module.descriptor_->outputChannelNames : module.descriptor_->inputChannelNames;
        if (ch < names.size()) return juce::String(names[ch]);
    }
    return juce::String(ch);
}

void RoutingView::drawView(juce::Graphics& g) {
    base_type::drawView(g);
    refresh();

    g.setFont(monoFont(FH));
    g.setColour(juce::Colours::yellow);
    g.drawSingleLineText("Track " + juce::String(trackIdx_ + 1) + " routing", 15 * COMPACT_UI_SCALE,
                         15 * COMPACT_UI_SCALE);
    if (wide()) {
        // soft key 1 toggles Level; with no button box, the title line says so
        g.setColour(levelMode_ ? juce::Colours::yellow : juce::Colours::grey);
        g.drawSingleLineText(juce::String("soft 1: Level ") + (levelMode_ ? "on" : "off"), 140 * COMPACT_UI_SCALE,
                             15 * COMPACT_UI_SCALE);
    }

    // Hosted directly by Synthor the view is 1600 x 480; hosted compact it is 640 x 480.
    // Full screen: the module grid fills the 640 px above the button bar; right of the bar, the
    // jack matrix, then the status and the track's wires, use the whole height.
    // Compact: the status goes under the jack matrix and there is no wire list.
    constexpr int S = COMPACT_UI_SCALE;
    int x = canvasX(), y = canvasY(), h = canvasHeight();
    int lh = FH + 4;
    if (wide()) {
        // cells grow as slots empty, up to 60 x 45 at scale 1
        int hw = 50 * S;
        int rh = std::min(h / int(nRows_ + 1), 45 * S);
        int cw = std::min((int(SSP_COMPACT_WIDTH) - x - hw - 5 * S) / int(nCols_), 60 * S);
        int gridW = drawGrid(g, x, y, rh, hw, cw, { 10 * S, std::min(16 * S, rh * 3 / 5), false, 0, 0, 0, 0 });
        // the jack matrix starts after the grid, so a track with few modules leaves it more width;
        // it ends where the wire list's 240 px (at scale 1) begins
        int lx = getWidth() - 245 * S;
        int jx = x + gridW + 10 * S, jy = 5 * S, jh = getHeight() - 2 * jy;
        int jw = lx - 10 * S - jx;
        drawJacks(g, jx, jy, jw, jh, { 0, 0, false, 60 * S, 55 * S, 40 * S, 10 * S });
        int lw = getWidth() - 5 * S - lx;
        drawStatus(g, lx, jy, lw);
        drawWireList(g, lx, jy + 3 * lh, lw, jh - 3 * lh);
    } else {
        int gridW = drawGrid(g, x, y, h / int(R + 1), 20 * S, 13 * S, { SMALL_FH, FH, true, 0, 0, 0, 0 });
        int jx = x + gridW + 6 * S;
        int jw = x + canvasWidth() - jx;
        int statusH = 2 * lh + 2 * S;
        drawJacks(g, jx, y, jw, h - statusH, { 0, 0, true, 33 * S, 35 * S, 14 * S, SMALL_FH });
        drawStatus(g, jx, y + h - statusH + 2 * S, jw);
    }
}

// the wire under the jack cursor, if any
const Matrix::Wire* RoutingView::cursorWire() const {
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ == src && wire.src_.chIdx_ == srcCh_ && wire.dest_.modIdx_ == dest
            && wire.dest_.chIdx_ == destCh_) {
            return &wire;
        }
    }
    return nullptr;
}

// text read bottom to top, its baseline end at (x, y): a column label
static void drawRotated(juce::Graphics& g, const juce::String& text, int x, int y, int length, int height) {
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, float(x), float(y)));
    g.drawText(text, x, y - height, length, height, juce::Justification::centredLeft, true);
}

static void drawBadge(juce::Graphics& g, unsigned enc, int x, int y, int h) {
    int w = 9 * COMPACT_UI_SCALE;
    g.setColour(juce::Colours::grey);
    g.fillRoundedRectangle(float(x), float(y + 2), float(w), float(h - 4), 3.0f);
    g.setColour(juce::Colours::black);
    g.setFont(monoFont(SMALL_FH));
    g.drawText(juce::String(enc), x, y, w, h, juce::Justification::centred, false);
}

// hw is the row header width, cw the cell width. Compact, column headers are rotated, since a module
// name does not fit across the cell. Returns the grid's width.
int RoutingView::drawGrid(juce::Graphics& g, int x, int y, int rh, int hw, int cw, const Sizes& sz) {
    auto centred = juce::Justification::centred;
    auto& modules = processor_.track(trackIdx_).modules_;

    // rows feed columns
    g.setFont(monoFont(std::min(sz.headerFh, rh / 2)));
    g.setColour(juce::Colours::grey);
    g.drawText("to", x, y, hw - 2, rh / 2, juce::Justification::centredRight, false);
    g.drawText("from", x, y + rh / 2, hw, rh / 2, juce::Justification::centredLeft, false);
    g.setFont(monoFont(sz.headerFh));

    auto headerColour = [&](unsigned midx, bool selected) {
        if (selected) return juce::Colours::yellow;
        bool empty = midx != Track::M_IN && midx != Track::M_OUT && modules[midx].pluginName_.empty();
        return empty ? juce::Colour(0xff555555) : juce::Colours::lightgrey;
    };
    for (unsigned i = 0; i < nRows_; i++) {
        g.setColour(headerColour(rows_[i], i == row_));
        g.drawFittedText(headerLabel(rows_[i]), x, y + int(i + 1) * rh, hw, rh, centred, 2, 0.7f);
    }
    for (unsigned i = 0; i < nCols_; i++) {
        g.setColour(headerColour(cols_[i], i == col_));
        if (sz.rotated) {
            auto label = headerLabel(cols_[i]).replaceCharacter('\n', ' ');
            drawRotated(g, label, x + hw + int(i) * cw + (cw + sz.headerFh) / 2, y + rh - 2, rh - 2, sz.headerFh);
        } else {
            g.drawFittedText(headerLabel(cols_[i]), x + hw + int(i) * cw, y, cw, rh, centred, 2, 0.7f);
        }
    }

    g.setFont(monoFont(sz.countFh));
    for (unsigned r = 0; r < nRows_; r++) {
        for (unsigned c = 0; c < nCols_; c++) {
            int cx = x + hw + int(c) * cw;
            int cy = y + int(r + 1) * rh;
            unsigned src = rows_[r];
            unsigned dest = cols_[c];

            if (src == dest) {
                g.setColour(juce::Colour(0xff222222));
                g.fillRect(cx, cy, cw, rh);
            }

            unsigned n = count_[src][dest];
            if (n == 0) {
                g.setColour(juce::Colour(0xff444444));
                g.fillRect(cx + cw / 2 - 1, cy + rh / 2 - 1, 3, 3);
            } else {
                g.setColour(kindColour(rack::wireKind(order_, src, dest)));
                g.drawText(juce::String(n), cx, cy, cw, rh, centred, false);
            }

            if (r == row_ && c == col_) {
                g.setColour(juce::Colours::yellow);
                g.drawRect(cx, cy, cw, rh, 3);
            }
        }
    }
    return hw + int(nCols_) * cw;
}

// a gain as short as it can be: 1, .8, -.25
static juce::String gainText(float v) {
    auto t = juce::String(v, 2);
    while (t.containsChar('.') && (t.endsWithChar('0') || t.endsWithChar('.'))) t = t.dropLastCharacters(1);
    return t.replace("0.", ".");
}

// The jack matrix of the selected module pair: rows are the source's outputs, columns the
// destination's inputs, and a cell's brightness is its gain. Badges name the encoder for each cursor.
void RoutingView::drawJacks(juce::Graphics& g, int x, int y, int w, int h, const Sizes& sz) {
    int labelW = sz.labelW, colLabelH = sz.colLabelH;
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    auto left = juce::Justification::centredLeft;
    int lh = FH + 4;
    int badgeW = 12 * COMPACT_UI_SCALE;

    // header: [1] source  [2] destination
    g.setFont(monoFont(FH));
    int half = w / 2;
    drawBadge(g, 1, x, y, lh);
    g.setFont(monoFont(FH));
    g.setColour(juce::Colours::yellow);
    g.drawText(moduleName(src) + " >", x + badgeW, y, half - badgeW, lh, left, true);
    drawBadge(g, 2, x + half, y, lh);
    g.setFont(monoFont(FH));
    g.setColour(juce::Colours::yellow);
    g.drawText(moduleName(dest), x + half + badgeW, y, w - half - badgeW, lh, left, true);

    unsigned nSrc = channelCount(src, true), nDest = channelCount(dest, false);
    srcCh_ = std::min(srcCh_, nSrc ? nSrc - 1 : 0);  // a module swap can shrink the jack lists
    destCh_ = std::min(destCh_, nDest ? nDest - 1 : 0);

    if (src == dest || nSrc == 0 || nDest == 0) {
        g.setColour(juce::Colours::grey);
        auto why = src == dest ? "same module" : nSrc == 0 ? "no outputs" : "no inputs";
        g.drawText(why, x, y + 2 * lh, w, lh, left, true);
        return;
    }

    // gains by jack pair; repeated wires sum, as in the engine
    std::vector<float> gain(nSrc * nDest, 0.0f);
    std::vector<bool> wired(nSrc * nDest, false);
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ != src || wire.dest_.modIdx_ != dest) continue;
        if (wire.src_.chIdx_ >= nSrc || wire.dest_.chIdx_ >= nDest) continue;
        gain[wire.src_.chIdx_ * nDest + wire.dest_.chIdx_] += wire.gain_;
        wired[wire.src_.chIdx_ * nDest + wire.dest_.chIdx_] = true;
    }

    // cells fill the area up to sz.maxCell a side, so small pairs are not tiny
    int ax = x + labelW, aw = w - labelW;
    int ay = y + lh + colLabelH, ah = y + h - ay;
    int jc = std::min(sz.maxCell, aw / int(nDest));
    int jr = std::min(sz.maxCell, ah / int(nSrc));
    int labelFH = std::min({ sz.maxLabelFh, jr - 2, jc - 2 });

    g.setFont(monoFont(labelFH));
    for (unsigned c = 0; c < nDest; c++) {
        g.setColour(c == destCh_ ? juce::Colours::yellow : juce::Colours::grey);
        drawRotated(g, channelName(dest, c, false), ax + int(c) * jc + (jc + labelFH) / 2, ay - 2, colLabelH - 4, labelFH);
    }
    for (unsigned r = 0; r < nSrc; r++) {
        g.setColour(r == srcCh_ ? juce::Colours::yellow : juce::Colours::grey);
        g.drawText(channelName(src, r, true), x, ay + int(r) * jr, labelW - 2, jr, left, true);
    }

    // the cursor's row and column, then the cells
    g.setColour(juce::Colour(0xff1a1a10));
    g.fillRect(ax, ay + int(srcCh_) * jr, jc * int(nDest), jr);
    g.fillRect(ax + int(destCh_) * jc, ay, jc, jr * int(nSrc));
    // the gain is printed in the cell when it fits, since brightness alone is hard to read
    auto colour = kindColour(rack::wireKind(order_, src, dest));
    // a monospaced glyph is about 0.6 of the font size wide, and ".25" is the usual longest gain
    int gainFh = std::min({ 14 * COMPACT_UI_SCALE, jr / 2, (jc - 4) * 5 / 9 });
    bool showGain = gainFh >= 7 * COMPACT_UI_SCALE;  // smaller is unreadable; brightness and the lists remain
    g.setFont(monoFont(gainFh));
    for (unsigned r = 0; r < nSrc; r++) {
        for (unsigned c = 0; c < nDest; c++) {
            int cx = ax + int(c) * jc, cy = ay + int(r) * jr;
            if (wired[r * nDest + c]) {
                float v = gain[r * nDest + c];
                float level = std::min(1.0f, std::abs(v));
                // opaque: the view is painted over its last frame, so a translucent fill would build
                // up to full brightness and stop showing the gain
                g.setColour(juce::Colour(0xff111111).interpolatedWith(colour, 0.25f + 0.75f * level));
                g.fillRoundedRectangle(float(cx + 2), float(cy + 2), float(jc - 4), float(jr - 4), 2.0f);
                if (showGain) {
                    g.setColour(level > 0.5f ? juce::Colours::black : juce::Colours::white);
                    g.drawText(gainText(v), cx, cy, jc, jr, juce::Justification::centred, false);
                }
            } else {
                g.setColour(juce::Colour(0xff444444));
                g.fillRect(cx + jc / 2 - 1, cy + jr / 2 - 1, 2, 2);
            }
        }
    }
    // the cursor: a thick yellow frame, with a black inner line so it shows on a bright cell
    int kx = ax + int(destCh_) * jc, ky = ay + int(srcCh_) * jr;
    g.setColour(juce::Colours::yellow);
    g.drawRect(kx - 1, ky - 1, jc + 2, jr + 2, 4);
    g.setColour(juce::Colours::black);
    g.drawRect(kx + 3, ky + 3, jc - 6, jr - 6, 1);
}

// two lines: the jack cursor, then its level; in Level mode the badges move to offset and gain
void RoutingView::drawStatus(juce::Graphics& g, int x, int sy, int w) {
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    if (src == dest || channelCount(src, true) == 0 || channelCount(dest, false) == 0) return;
    auto left = juce::Justification::centredLeft;
    int lh = FH + 4;
    int badgeW = 12 * COMPACT_UI_SCALE;
    int half = w / 2;
    auto cursor = cursorWire();
    juce::String srcName = channelName(src, srcCh_, true), destName = channelName(dest, destCh_, false);
    juce::String gainText = "x" + juce::String(cursor ? cursor->gain_ : 0.0f, 2);
    juce::String offsetText = "+" + juce::String(cursor ? cursor->offset_ : 0.0f, 2);
    if (!levelMode_) {
        drawBadge(g, 3, x, sy, lh);
        g.setFont(monoFont(FH));
        g.setColour(juce::Colours::white);
        g.drawText(srcName + " ->", x + badgeW, sy, half - badgeW, lh, left, true);
        drawBadge(g, 4, x + half, sy, lh);
        g.setFont(monoFont(FH));
        g.setColour(juce::Colours::white);
        g.drawText(destName, x + half + badgeW, sy, w - half - badgeW, lh, left, true);
        g.setColour(cursor ? juce::Colours::white : juce::Colours::grey);
        g.drawText(cursor ? gainText + " " + offsetText : "not wired", x + badgeW, sy + lh, w - badgeW, lh, left, true);
    } else {
        g.setFont(monoFont(FH));
        g.setColour(juce::Colours::white);
        g.drawText(srcName + " -> " + destName, x + badgeW, sy, w - badgeW, lh, left, true);
        drawBadge(g, 3, x, sy + lh, lh);
        g.setFont(monoFont(FH));
        g.setColour(cursor ? juce::Colours::white : juce::Colours::grey);
        g.drawText(cursor ? offsetText : "-", x + badgeW, sy + lh, half - badgeW, lh, left, true);
        drawBadge(g, 4, x + half, sy + lh, lh);
        g.setFont(monoFont(FH));
        g.setColour(juce::Colours::white);
        g.drawText(gainText, x + half + badgeW, sy + lh, w - half - badgeW, lh, left, true);
    }
}

// Every wire on the track, in execution order, as "omod Main -> drum HH1 Trig  x1.00". The selected
// module pair's wires are white and the cursor's wire yellow; the list scrolls to keep it in view.
void RoutingView::drawWireList(juce::Graphics& g, int x, int y, int w, int h) {
    auto left = juce::Justification::centredLeft;
    int lh = FH + 4;
    unsigned pos[N] = {};
    for (unsigned i = 0; i < N; i++) pos[order_[i]] = i;
    std::vector<const Matrix::Wire*> list;
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ < N && wire.dest_.modIdx_ < N) list.push_back(&wire);
    }
    std::stable_sort(list.begin(), list.end(), [&](const Matrix::Wire* a, const Matrix::Wire* b) {
        return std::make_tuple(pos[a->src_.modIdx_], a->src_.chIdx_, pos[a->dest_.modIdx_], a->dest_.chIdx_)
               < std::make_tuple(pos[b->src_.modIdx_], b->src_.chIdx_, pos[b->dest_.modIdx_], b->dest_.chIdx_);
    });

    auto jack = [&](unsigned midx, unsigned ch, bool output) {
        return headerLabel(midx).replaceCharacter('\n', '#') + " " + channelName(midx, ch, output);
    };
    int pad = 0;
    for (auto* wire : list) pad = std::max(pad, jack(wire->src_.modIdx_, wire->src_.chIdx_, true).length());

    g.setFont(monoFont(SMALL_FH));
    g.setColour(juce::Colours::grey);
    g.drawText(juce::String(int(list.size())) + " wires on this track", x, y, w, lh, left, true);
    y += lh;
    g.setFont(monoFont(FH));

    unsigned rows = unsigned(std::max(1, h / lh - 1));  // one line kept for the overflow note
    auto cursor = cursorWire();
    size_t first = 0;
    auto at = std::find(list.begin(), list.end(), cursor);
    if (at != list.end() && size_t(at - list.begin()) >= rows) first = size_t(at - list.begin()) - rows + 1;

    unsigned src = rows_[row_], dest = cols_[col_];
    for (size_t i = first; i < list.size() && i < first + rows; i++) {
        auto& wire = *list[i];
        bool selected = wire.src_.modIdx_ == src && wire.dest_.modIdx_ == dest;
        g.setColour(&wire == cursor ? juce::Colours::yellow : selected ? juce::Colours::white : juce::Colours::grey);
        juce::String line = jack(wire.src_.modIdx_, wire.src_.chIdx_, true).paddedRight(' ', pad) + " -> "
                            + jack(wire.dest_.modIdx_, wire.dest_.chIdx_, false);
        juce::String amount = "x" + juce::String(wire.gain_, 2);
        if (wire.offset_ != 0.0f) amount << " +" << juce::String(wire.offset_, 2);
        int amountW = 12 * FH * 6 / 10;  // "x1.00 +0.00" in a monospace face
        int ly = y + int(i - first) * lh;
        g.drawText(line, x, ly, w - amountW, lh, left, true);
        g.drawText(amount, x + w - amountW, ly, amountW, lh, left, false);
    }
    if (first > 0 || list.size() > first + rows) {
        g.setColour(juce::Colours::grey);
        g.setFont(monoFont(SMALL_FH));
        size_t shown = std::min(size_t(rows), list.size() - first);
        g.drawText(juce::String(int(list.size() - shown)) + " more, move the cursor to scroll", x, y + int(rows) * lh, w,
                   lh, left, true);
    }
}
