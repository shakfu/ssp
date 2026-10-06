#pragma once

#include <array>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "ssp/editors/BaseMiniView.h"

// Routing of one track: a module grid in execution order, and the wires of the selected cell, with a
// jack cursor to add, remove and scale them. See docs/dev/rack-design.md, section 6.
class RoutingView : public ssp::MiniBasicView {
public:
    explicit RoutingView(PluginProcessor& p);

    void trackIdx(unsigned t) { trackIdx_ = t; }

    void onEncoder(unsigned enc, float v) override;
    void onEncoderSwitch(unsigned enc, bool v) override;

protected:
    void drawView(juce::Graphics& g) override;
    void editorShown() override;

private:
    using base_type = ssp::MiniBasicView;
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
    void drawGrid(juce::Graphics& g, int x, int y, int cell);
    void drawDetail(juce::Graphics& g, int x, int y, int w, int h);

    PluginProcessor& processor_;
    std::shared_ptr<ssp::ValueButton> levelBtn_;
    unsigned trackIdx_ = 0;
    unsigned row_ = 0;  // cursor, as positions in rows_ and cols_
    unsigned col_ = 0;
    unsigned srcCh_ = 0;  // jack cursor within the selected cell
    unsigned destCh_ = 0;
    bool levelMode_ = false;  // encoders 3 and 4 set offset and gain instead of moving the jack cursor;
                              // gain is the connection: 0 means no wire

    std::vector<Matrix::Wire> wires_;
    std::array<unsigned, N> order_{};
    std::array<unsigned, R> rows_{};  // sources: order_ without OUT
    std::array<unsigned, R> cols_{};  // destinations: order_ without IN
    unsigned count_[N][N] = {};       // wires per module pair, by module index
};
