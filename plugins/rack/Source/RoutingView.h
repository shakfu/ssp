#pragma once

#include <array>
#include <vector>

#include "PluginProcessor.h"
#include "ssp/editors/BaseMiniView.h"

// Read-only routing of one track: a module grid in execution order, and the wires of the selected
// cell. See docs/dev/rack-design.md, section 6.
class RoutingView : public ssp::MiniBasicView {
public:
    explicit RoutingView(PluginProcessor& p);

    void trackIdx(unsigned t) { trackIdx_ = t; }

    void onEncoder(unsigned enc, float v) override;

protected:
    void drawView(juce::Graphics& g) override;
    void editorShown() override;

private:
    using base_type = ssp::MiniBasicView;
    static constexpr unsigned N = Track::M_MAX;

    void refresh();
    juce::String slotLabel(unsigned midx) const;
    juce::String channelName(unsigned midx, unsigned ch, bool output);
    void drawGrid(juce::Graphics& g, int x, int y, int cell);
    void drawDetail(juce::Graphics& g, int x, int y, int w, int h);

    PluginProcessor& processor_;
    unsigned trackIdx_ = 0;
    unsigned row_ = 0;  // cursor, as positions in order_
    unsigned col_ = 0;
    unsigned scroll_ = 0;

    std::vector<Matrix::Wire> wires_;
    std::array<unsigned, N> order_{};
    unsigned count_[N][N] = {};  // wires per module pair, by module index
};
