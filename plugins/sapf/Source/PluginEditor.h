#pragma once

#include "PluginProcessor.h"
#include "ssp/BaseView.h"
#include "ssp/controls/VuMeter.h"

// Keys from the queue into the code, and the actions they ask for. Shared by both editors: the instance
// whose editor runs takes the keyboard.
class KeyPump {
public:
    explicit KeyPump(PluginProcessor& p) : processor_(p) {}
    void tick();  // the editor's timer
    // the rows an evaluation sent, shown for a moment
    bool flashing(int& first, int& last) const;

private:
    PluginProcessor& processor_;
    int flashFirst_ = 0, flashLast_ = -1;
    juce::uint32 flashUntil_ = 0;
};

// The full screen: the code, soft-wrapped, above the output and a status line.
class PluginEditor : public ssp::BaseView {
public:
    explicit PluginEditor(PluginProcessor&);
    void drawView(Graphics&) override;

protected:
    void onSSPTimer() override;
    void onEncoder(unsigned enc, float v) override;  // 1: rows, 2: columns

private:
    PluginProcessor& processor_;
    KeyPump pump_;
    int top_ = 0;  // the first screen row of the code shown
    // inputs 1-2, then outputs 1-2, right of the output pane
    ssp::MonoVuMeter meters_[2 * PluginProcessor::METERED];
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

// compact editor for rack: the status and the last output lines
class PluginMiniEditor : public ssp::BaseView {
public:
    explicit PluginMiniEditor(PluginProcessor&);
    void drawView(Graphics&) override;

protected:
    void onSSPTimer() override { pump_.tick(); }

private:
    PluginProcessor& processor_;
    KeyPump pump_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMiniEditor)
};
