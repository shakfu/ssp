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
    return juce::String(name) + (isDuplicate(midx) ? " (slot " + slot + ")" : "");
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

    // header row and column, then one cell per module pair
    int cell = canvasHeight() / int(R + 1);
    int gridW = cell * int(R + 1);
    drawGrid(g, canvasX(), canvasY(), cell);

    int gap = 5 * COMPACT_UI_SCALE;
    int dx = canvasX() + gridW + gap;
    drawDetail(g, dx, canvasY(), canvasX() + canvasWidth() - dx, canvasHeight());
}

void RoutingView::drawGrid(juce::Graphics& g, int x, int y, int cell) {
    auto centred = juce::Justification::centred;
    auto& modules = processor_.track(trackIdx_).modules_;

    // rows feed columns
    g.setFont(monoFont(SMALL_FH));
    g.setColour(juce::Colours::grey);
    g.drawText("to", x, y, cell - 2, cell / 2, juce::Justification::centredRight, false);
    g.drawText("from", x, y + cell / 2, cell, cell / 2, juce::Justification::centredLeft, false);

    auto headerColour = [&](unsigned midx, bool selected) {
        if (selected) return juce::Colours::yellow;
        bool empty = midx != Track::M_IN && midx != Track::M_OUT && modules[midx].pluginName_.empty();
        return empty ? juce::Colour(0xff555555) : juce::Colours::lightgrey;
    };
    for (unsigned i = 0; i < R; i++) {
        g.setColour(headerColour(rows_[i], i == row_));
        g.drawFittedText(headerLabel(rows_[i]), x, y + int(i + 1) * cell, cell, cell, centred, 2, 0.7f);
        g.setColour(headerColour(cols_[i], i == col_));
        g.drawFittedText(headerLabel(cols_[i]), x + int(i + 1) * cell, y, cell, cell, centred, 2, 0.7f);
    }

    g.setFont(monoFont(FH));
    for (unsigned r = 0; r < R; r++) {
        for (unsigned c = 0; c < R; c++) {
            int cx = x + int(c + 1) * cell;
            int cy = y + int(r + 1) * cell;
            unsigned src = rows_[r];
            unsigned dest = cols_[c];

            if (src == dest) {
                g.setColour(juce::Colour(0xff222222));
                g.fillRect(cx, cy, cell, cell);
            }

            unsigned n = count_[src][dest];
            if (n == 0) {
                g.setColour(juce::Colour(0xff444444));
                g.fillRect(cx + cell / 2 - 1, cy + cell / 2 - 1, 3, 3);
            } else {
                g.setColour(kindColour(rack::wireKind(order_, src, dest)));
                g.drawText(juce::String(n), cx, cy, cell, cell, centred, false);
            }

            if (r == row_ && c == col_) {
                g.setColour(juce::Colours::yellow);
                g.drawRect(cx, cy, cell, cell, 2);
            }
        }
    }
}

// Each of the first four lines starts with a badge naming the encoder that changes it.
void RoutingView::drawDetail(juce::Graphics& g, int x, int y, int w, int h) {
    unsigned src = rows_[row_];
    unsigned dest = cols_[col_];
    auto left = juce::Justification::centredLeft;
    int lh = FH + 4;

    unsigned nSrc = channelCount(src, true), nDest = channelCount(dest, false);
    srcCh_ = std::min(srcCh_, nSrc ? nSrc - 1 : 0);  // a module swap can shrink the jack lists
    destCh_ = std::min(destCh_, nDest ? nDest - 1 : 0);
    const Matrix::Wire* cursor = nullptr;
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ == src && wire.src_.chIdx_ == srcCh_ && wire.dest_.modIdx_ == dest
            && wire.dest_.chIdx_ == destCh_) {
            cursor = &wire;
            break;
        }
    }
    bool hasJacks = nSrc > 0 && nDest > 0 && src != dest;

    int badgeW = FH;
    int tx = x + badgeW + 3 * COMPACT_UI_SCALE;
    int tw = w - (tx - x);
    auto line = [&](int i, unsigned enc, const juce::String& text, juce::Colour colour) {
        int ly = y + i * lh;
        g.setColour(juce::Colours::grey);
        g.fillRoundedRectangle(float(x), float(ly + 2), float(badgeW), float(lh - 4), 3.0f);
        g.setColour(juce::Colours::black);
        g.drawText(juce::String(enc), x, ly, badgeW, lh, juce::Justification::centred, false);
        g.setColour(colour);
        g.drawText(text, tx, ly, tw, lh, left, true);
    };

    g.setFont(monoFont(FH));
    line(0, 1, moduleName(src), juce::Colours::yellow);
    line(1, 2, moduleName(dest), juce::Colours::yellow);
    juce::String gainText = "x" + juce::String(cursor ? cursor->gain_ : 0.0f, 2) + " gain";
    juce::String offsetText = "+" + juce::String(cursor ? cursor->offset_ : 0.0f, 2) + " offset";
    if (levelMode_) {
        auto colour = cursor ? juce::Colours::white : juce::Colours::grey;
        line(2, 3, cursor ? offsetText : "-", colour);
        line(3, 4, gainText, hasJacks ? juce::Colours::white : juce::Colours::grey);
    } else {
        line(2, 3, nSrc ? channelName(src, srcCh_, true) : "no outputs", juce::Colours::white);
        line(3, 4, nDest ? channelName(dest, destCh_, false) : "no inputs", juce::Colours::white);
    }

    // the wire under the cursor: its level, or in level mode its jacks
    int ly = y + 4 * lh;
    juce::String status;
    if (src == dest) {
        status = "same module";
    } else if (!hasJacks) {
        status = "";
    } else if (!cursor) {
        status = "not wired";
    } else if (levelMode_) {
        status = channelName(src, srcCh_, true) + "->" + channelName(dest, destCh_, false);
    } else {
        status = "x" + juce::String(cursor->gain_, 2) + " +" + juce::String(cursor->offset_, 2);
    }
    g.setColour(cursor ? juce::Colours::white : juce::Colours::grey);
    g.drawText(status, tx, ly, tw, lh, left, true);
    ly += lh;

    auto kind = rack::wireKind(order_, src, dest);
    if (count_[src][dest] > 0 && kind != rack::WireKind::Forward) {
        g.setColour(kindColour(kind));
        g.drawText(kind == rack::WireKind::Delayed ? "1 block late" : "no signal", tx, ly, tw, lh, left, true);
    }
    ly += lh + lh / 2;

    // the cell's wires, source names padded so the arrows line up; gain and offset on a second line
    // if set, since long jack names leave no room for them on one line.
    std::vector<const Matrix::Wire*> cellWires;
    int pad = 0;
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ != src || wire.dest_.modIdx_ != dest) continue;
        cellWires.push_back(&wire);
        pad = std::max(pad, channelName(src, wire.src_.chIdx_, true).length());
    }
    auto amountOf = [](const Matrix::Wire& wire) {
        juce::String amount;
        if (wire.gain_ != 1.0f) amount << "x" << juce::String(wire.gain_, 2) << " ";
        if (wire.offset_ != 0.0f) amount << "+" << juce::String(wire.offset_, 2);
        return amount;
    };
    auto height = [&](const Matrix::Wire* wire) { return amountOf(*wire).isEmpty() ? lh : 2 * lh; };

    int bottom = y + h - lh;  // keep a line for the overflow note
    // end the list on the cursor's wire when it would not fit from the top
    size_t first = 0;
    auto at = std::find(cellWires.begin(), cellWires.end(), cursor);
    if (at != cellWires.end()) {
        first = size_t(at - cellWires.begin()) + 1;
        for (int used = 0; first > 0 && ly + used + height(cellWires[first - 1]) <= bottom; first--) {
            used += height(cellWires[first - 1]);
        }
    }
    for (size_t i = first; i < cellWires.size(); i++) {
        auto& wire = *cellWires[i];
        auto amount = amountOf(wire);
        if (ly + height(&wire) > bottom) {
            g.setColour(juce::Colours::grey);
            g.drawText("+" + juce::String(int(cellWires.size() - i)) + " more", x, bottom, w, lh, left, true);
            break;
        }
        g.setColour(&wire == cursor ? juce::Colours::yellow : juce::Colours::white);
        auto srcName = channelName(src, wire.src_.chIdx_, true).paddedRight(' ', pad);
        g.drawText(srcName + " -> " + channelName(dest, wire.dest_.chIdx_, false), x, ly, w, lh, left, true);
        ly += lh;
        if (amount.isNotEmpty()) {
            g.setColour(juce::Colours::grey);
            g.drawText(amount, x + FH, ly, w - FH, lh, left, true);
            ly += lh;
        }
    }
}
