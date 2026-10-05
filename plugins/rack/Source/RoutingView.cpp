#include "RoutingView.h"

#include "GraphOrder.h"

static constexpr int FH = 10 * COMPACT_UI_SCALE;

static juce::Colour kindColour(rack::WireKind kind) {
    switch (kind) {
        case rack::WireKind::Forward: return juce::Colours::lightgreen;
        case rack::WireKind::Delayed: return juce::Colours::orange;
        case rack::WireKind::Self: return juce::Colours::red;
    }
    return juce::Colours::white;
}

RoutingView::RoutingView(PluginProcessor& p) : base_type(&p, nullptr), processor_(p) {
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
}

void RoutingView::editorShown() {
    base_type::editorShown();
    refresh();
    // start on the first wired cell, so the detail pane shows something
    row_ = col_ = scroll_ = 0;
    for (unsigned r = 0; r < N; r++) {
        for (unsigned c = 0; c < N; c++) {
            if (count_[order_[r]][order_[c]] > 0) {
                row_ = r;
                col_ = c;
                return;
            }
        }
    }
}

void RoutingView::onEncoder(unsigned enc, float v) {
    int step = v > 0 ? 1 : -1;
    auto move = [step](unsigned& pos, unsigned limit) {
        int next = int(pos) + step;
        if (next >= 0 && next < int(limit)) pos = unsigned(next);
    };
    switch (enc) {
        case 0:
            move(row_, N);
            scroll_ = 0;
            break;
        case 1:
            move(col_, N);
            scroll_ = 0;
            break;
        case 2: move(scroll_, count_[order_[row_]][order_[col_]]); break;
        default: break;
    }
}

juce::String RoutingView::slotLabel(unsigned midx) const {
    if (midx == Track::M_IN) return "IN";
    if (midx == Track::M_OUT) return "OUT";
    return juce::String(midx - Track::M_SLOT_1 + 1);
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

    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), FH, juce::Font::plain)));
    g.setColour(juce::Colours::yellow);
    g.drawSingleLineText("Track " + juce::String(trackIdx_ + 1) + " routing", 15 * COMPACT_UI_SCALE,
                         15 * COMPACT_UI_SCALE);

    // header row and column, then one cell per module pair
    int cell = canvasHeight() / int(N + 1);
    int gridW = cell * int(N + 1);
    drawGrid(g, canvasX(), canvasY(), cell);

    int gap = 5 * COMPACT_UI_SCALE;
    int dx = canvasX() + gridW + gap;
    drawDetail(g, dx, canvasY(), canvasX() + canvasWidth() - dx, canvasHeight());
}

void RoutingView::drawGrid(juce::Graphics& g, int x, int y, int cell) {
    auto centred = juce::Justification::centred;
    for (unsigned i = 0; i < N; i++) {
        auto label = slotLabel(order_[i]);
        g.setColour(i == row_ ? juce::Colours::yellow : juce::Colours::grey);
        g.drawText(label, x, y + int(i + 1) * cell, cell, cell, centred, false);
        g.setColour(i == col_ ? juce::Colours::yellow : juce::Colours::grey);
        g.drawText(label, x + int(i + 1) * cell, y, cell, cell, centred, false);
    }

    for (unsigned r = 0; r < N; r++) {
        for (unsigned c = 0; c < N; c++) {
            int cx = x + int(c + 1) * cell;
            int cy = y + int(r + 1) * cell;
            unsigned src = order_[r];
            unsigned dest = order_[c];

            if (r == c) {
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

void RoutingView::drawDetail(juce::Graphics& g, int x, int y, int w, int h) {
    unsigned src = order_[row_];
    unsigned dest = order_[col_];
    auto& modules = processor_.track(trackIdx_).modules_;
    auto left = juce::Justification::centredLeft;

    auto name = [&](unsigned midx) {
        auto& pn = modules[midx].pluginName_;
        return slotLabel(midx) + (midx == Track::M_IN || midx == Track::M_OUT || pn.empty() ? "" : " " + pn);
    };

    int lh = FH + 4;
    g.setColour(juce::Colours::yellow);
    g.drawText(name(src) + " >", x, y, w, lh, left, true);
    g.drawText(name(dest), x, y + lh, w, lh, left, true);

    auto kind = rack::wireKind(order_, src, dest);
    unsigned n = count_[src][dest];
    if (n > 0 && kind != rack::WireKind::Forward) {
        g.setColour(kindColour(kind));
        g.drawText(kind == rack::WireKind::Delayed ? "1 block late" : "no signal", x, y + 2 * lh, w, lh, left, true);
    }

    // per wire: source and destination channel, then gain and offset on a second line if set.
    // The pane is about 18 characters wide, too narrow for all three on one line.
    int ly = y + 3 * lh + lh / 2;
    int bottom = y + h - lh;  // keep a line for the overflow note
    int nameW = w / 2;
    unsigned skipped = 0;
    for (auto& wire : wires_) {
        if (wire.src_.modIdx_ != src || wire.dest_.modIdx_ != dest) continue;
        if (skipped++ < scroll_) continue;

        juce::String amount;
        if (wire.gain_ != 1.0f) amount << "x" << juce::String(wire.gain_, 2) << " ";
        if (wire.offset_ != 0.0f) amount << "+" << juce::String(wire.offset_, 2);
        int need = amount.isEmpty() ? lh : 2 * lh;
        if (ly + need > bottom) {
            g.setColour(juce::Colours::grey);
            g.drawText("more: enc 3", x, bottom, w, lh, left, true);
            break;
        }

        g.setColour(juce::Colours::white);
        g.drawText(channelName(src, wire.src_.chIdx_, true), x, ly, nameW, lh, left, true);
        g.drawText(channelName(dest, wire.dest_.chIdx_, false), x + nameW, ly, w - nameW, lh, left, true);
        ly += lh;
        if (amount.isNotEmpty()) {
            g.setColour(juce::Colours::grey);
            g.drawText(amount, x + FH, ly, w - FH, lh, left, true);
            ly += lh;
        }
    }
}
