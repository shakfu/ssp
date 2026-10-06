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

    unsigned r = 0, c = 0;
    for (auto m : order_) {
        if (m != Track::M_OUT) rows_[r++] = m;
        if (m != Track::M_IN) cols_[c++] = m;
    }
}

void RoutingView::editorShown() {
    base_type::editorShown();
    levelBtn_->value(false);
    refresh();
    // start on the first wired cell, so the detail pane shows something
    row_ = col_ = 0;
    for (unsigned r = 0; r < R && !count_[rows_[row_]][cols_[col_]]; r++) {
        for (unsigned c = 0; c < R; c++) {
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
    switch (enc) {
        case 0:
            move(row_, R);
            refresh();
            resetCursor();
            break;
        case 1:
            move(col_, R);
            refresh();
            resetCursor();
            break;
        case 2:
            if (levelMode_) {
                while (!track.requestMatrixAttenuate(srcJack, destJack, true, v / 100.0f)) {}
            } else {
                move(srcCh_, channelCount(src, true));
            }
            break;
        case 3:
            if (levelMode_) {
                if (src != dest) {
                    while (!track.requestMatrixGain(srcJack, destJack, v / 100.0f)) {}
                }
            } else {
                move(destCh_, channelCount(dest, false));
            }
            break;
        default: break;
    }
}

// pushing encoder 4 sets the cursor wire's gain to 1, or to 0, which removes it
void RoutingView::onEncoderSwitch(unsigned enc, bool v) {
    if (enc == 3 && v) toggleWire();
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

    // Hosted directly by Synthor the view is 1600 px wide; hosted compact it is 640. Wide: square
    // module cells, full jack names, and a third column for the status and the track's wires.
    // Compact: the status goes under the jack matrix and there is no wire list.
    constexpr int S = COMPACT_UI_SCALE;
    bool wide = getWidth() > int(SSP_COMPACT_WIDTH);
    int x = canvasX(), y = canvasY(), h = canvasHeight();
    int rh = h / int(R + 1);
    int gridW = drawGrid(g, x, y, h, wide ? 30 * S : 20 * S, wide ? rh : 13 * S);
    int jx = x + gridW + 6 * S;
    int lh = FH + 4;
    if (wide) {
        int jw = 50 * S + 17 * 14 * S;  // fits the largest pair in modules.json, 16 x 17 jacks, at full size
        drawJacks(g, jx, y, jw, h, 50 * S, 45 * S);
        int lx = jx + jw + 12 * S;
        int lw = x + canvasWidth() - lx;
        drawStatus(g, lx, y, lw);
        drawWireList(g, lx, y + 3 * lh, lw, h - 3 * lh);
    } else {
        int jw = x + canvasWidth() - jx;
        int statusH = 2 * lh + 2 * S;
        drawJacks(g, jx, y, jw, h - statusH, 33 * S, 35 * S);
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

// hw is the row header width, cw the cell width; column headers are rotated, since a module name
// does not fit across a compact cell. Returns the grid's width.
int RoutingView::drawGrid(juce::Graphics& g, int x, int y, int h, int hw, int cw) {
    auto centred = juce::Justification::centred;
    auto& modules = processor_.track(trackIdx_).modules_;
    int rh = h / int(R + 1);

    // rows feed columns
    g.setFont(monoFont(SMALL_FH));
    g.setColour(juce::Colours::grey);
    g.drawText("to", x, y, hw - 2, rh / 2, juce::Justification::centredRight, false);
    g.drawText("from", x, y + rh / 2, hw, rh / 2, juce::Justification::centredLeft, false);

    auto headerColour = [&](unsigned midx, bool selected) {
        if (selected) return juce::Colours::yellow;
        bool empty = midx != Track::M_IN && midx != Track::M_OUT && modules[midx].pluginName_.empty();
        return empty ? juce::Colour(0xff555555) : juce::Colours::lightgrey;
    };
    for (unsigned i = 0; i < R; i++) {
        g.setColour(headerColour(rows_[i], i == row_));
        g.drawFittedText(headerLabel(rows_[i]), x, y + int(i + 1) * rh, hw, rh, centred, 2, 0.7f);
        g.setColour(headerColour(cols_[i], i == col_));
        auto label = headerLabel(cols_[i]).replaceCharacter('\n', ' ');
        drawRotated(g, label, x + hw + int(i) * cw + (cw + SMALL_FH) / 2, y + rh - 2, rh - 2, SMALL_FH);
    }

    g.setFont(monoFont(FH));
    for (unsigned r = 0; r < R; r++) {
        for (unsigned c = 0; c < R; c++) {
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
                g.drawRect(cx, cy, cw, rh, 2);
            }
        }
    }
    return hw + int(R) * cw;
}

// The jack matrix of the selected module pair: rows are the source's outputs, columns the
// destination's inputs, and a cell's brightness is its gain. Badges name the encoder for each cursor.
void RoutingView::drawJacks(juce::Graphics& g, int x, int y, int w, int h, int labelW, int colLabelH) {
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

    // cells fill the area up to 14 px a side at scale 1, so small pairs are not tiny
    int ax = x + labelW, aw = w - labelW;
    int ay = y + lh + colLabelH, ah = y + h - ay;
    int maxCell = 14 * COMPACT_UI_SCALE;
    int jc = std::min(maxCell, aw / int(nDest));
    int jr = std::min(maxCell, ah / int(nSrc));
    int labelFH = std::min(SMALL_FH, jr - 2);

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
    auto colour = kindColour(rack::wireKind(order_, src, dest));
    for (unsigned r = 0; r < nSrc; r++) {
        for (unsigned c = 0; c < nDest; c++) {
            int cx = ax + int(c) * jc, cy = ay + int(r) * jr;
            if (wired[r * nDest + c]) {
                float level = std::min(1.0f, std::abs(gain[r * nDest + c]));
                g.setColour(colour.withAlpha(0.25f + 0.75f * level));
                g.fillRoundedRectangle(float(cx + 2), float(cy + 2), float(jc - 4), float(jr - 4), 2.0f);
            } else {
                g.setColour(juce::Colour(0xff444444));
                g.fillRect(cx + jc / 2 - 1, cy + jr / 2 - 1, 2, 2);
            }
        }
    }
    g.setColour(juce::Colours::yellow);
    g.drawRect(ax + int(destCh_) * jc, ay + int(srcCh_) * jr, jc, jr, 2);
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
