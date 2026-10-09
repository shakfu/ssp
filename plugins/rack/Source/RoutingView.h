#pragma once

#include <array>
#include <memory>
#include <tuple>
#include <vector>

#include "PluginProcessor.h"
#include "ssp/editors/BaseMiniView.h"

// Routing of one track: a module grid in execution order beside the jack matrix of the selected
// cell, with a jack cursor to add, remove and scale wires. See docs/dev/rack-design.md, section 6.
class RoutingView : public ssp::MiniBasicView {
public:
    explicit RoutingView(PluginProcessor& p);

    void trackIdx(unsigned t) { trackIdx_ = t; }

    void onEncoder(unsigned enc, float v) override;
    void onEncoderSwitch(unsigned enc, bool v) override;

    // the base class assumes the compact width; hosted full screen this view is 1600 px wide
    int canvasWidth() override { return getWidth() - 2 * 5 * COMPACT_UI_SCALE; }
    // full screen there is no button box, so the canvas runs to the bottom
    int canvasHeight() override { return wide() ? getHeight() - canvasY() - 5 * COMPACT_UI_SCALE : base_type::canvasHeight(); }
    void resized() override;

protected:
    void drawView(juce::Graphics& g) override;
    void editorShown() override;

private:
    using base_type = ssp::MiniBasicView;
    bool wide() const { return getWidth() > int(SSP_COMPACT_WIDTH); }
    static constexpr unsigned N = Track::M_MAX;
    static constexpr unsigned R = N - 1;  // IN has no inputs and OUT no outputs, so each drops from one axis

    void refresh();
    void resetCursor();
    void toggleWire();
    bool isDuplicate(unsigned midx) const;
    juce::String headerLabel(unsigned midx) const;
    juce::String moduleName(unsigned midx) const;
    juce::String channelName(unsigned midx, unsigned ch, bool output);
    unsigned channelCount(unsigned midx, bool output);
    const Matrix::Wire* cursorWire() const;
    // sizes, in px, of the module grid and the jack matrix; compact and full screen differ
    struct Sizes {
        int headerFh, countFh;  // grid headers and wire counts
        bool rotated;           // grid column headers read bottom to top
        int labelW, colLabelH;  // jack matrix label areas
        int maxCell, maxLabelFh;
    };
    // rh is the row height
    int drawGrid(juce::Graphics& g, int x, int y, int rh, int hw, int cw, const Sizes& sz);
    void drawJacks(juce::Graphics& g, int x, int y, int w, int h, const Sizes& sz);
    void drawStatus(juce::Graphics& g, int x, int y, int w);
    void drawWireList(juce::Graphics& g, int x, int y, int w, int h);

    PluginProcessor& processor_;
    std::shared_ptr<ssp::ValueButton> levelBtn_;
    unsigned trackIdx_ = 0;
    unsigned row_ = 0;  // cursor, as positions in rows_ and cols_
    unsigned col_ = 0;
    unsigned srcCh_ = 0;  // jack cursor within the selected cell
    unsigned destCh_ = 0;
    bool levelMode_ = false;  // encoders 3 and 4 set offset and gain instead of moving the jack cursor;
                              // gain is the connection: 0 means no wire
    bool encDown_[4] = {};    // held encoders: turning one steps finely
    bool encTurned_[4] = {};  // turned while held, so its release is not a press

    std::vector<Matrix::Wire> wires_;
    std::array<unsigned, N> order_{};
    std::array<unsigned, R> rows_{};  // sources: order_ without OUT or empty slots
    std::array<unsigned, R> cols_{};  // destinations: order_ without IN or empty slots
    unsigned nRows_ = 0, nCols_ = 0;  // how many of rows_ and cols_ are used
    unsigned count_[N][N] = {};       // wires per module pair, by module index
};
