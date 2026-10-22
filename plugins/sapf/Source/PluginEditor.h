#pragma once

#include "PluginProcessor.h"
#include "ssp/BaseView.h"
#include "ssp/controls/VuMeter.h"

// The script editor's pages and Code view. sapf has no controls, so the pages show its output, left,
// and level meters for inputs and outputs 1-2 below the status panel.
class PluginEditor : public ssp::engine::ScriptEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    void onSSPTimer() override;

protected:
    void drawView(Graphics&) override;
    void setView(unsigned newView) override;  // the meters show on the pages only

private:
    PluginProcessor& processor_;
    // inputs 1-2, then outputs 1-2
    ssp::MonoVuMeter meters_[2 * PluginProcessor::METERED];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

// compact editor for rack: the status and the last output lines
class PluginMiniEditor : public ssp::BaseView {
public:
    explicit PluginMiniEditor(PluginProcessor&);
    void drawView(Graphics&) override;

private:
    PluginProcessor& processor_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMiniEditor)
};
