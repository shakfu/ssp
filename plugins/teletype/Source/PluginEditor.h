#pragma once

#include "PluginProcessor.h"
#include "ssp/EditorHost.h"
#include "ssp/controls/ValueButton.h"
#include "ssp/editors/BaseEditor.h"

// teletype's 128x64 screen as an image, rebuilt when the firmware redraws
class ScreenImage {
public:
    ScreenImage();
    void update(ttyp::Teletype& tt);
    void draw(juce::Graphics& g, juce::Rectangle<int> area) const;

private:
    juce::Image image_;
    uint32_t seq_ = ~0u;
    uint8_t px_[ttyp::Teletype::SCREEN_W * ttyp::Teletype::SCREEN_H] = {};
};

// Shared by both editors: the instance whose editor runs takes the keyboard. Main page: encoder 1 is
// PARAM, encoder 2 the In range. Range page: encoders 1-4 are the CV outputs' ranges.
void onTeletypeEncoder(PluginProcessor& p, unsigned enc, float v, bool rangePage);
void onTeletypeTimer(PluginProcessor& p, ScreenImage& screen);

class PluginEditor : public ssp::BaseEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;
    void drawView(Graphics&) override;

protected:
    void onSSPTimer() override;
    void onEncoder(unsigned enc, float v) override;
    void onButton(unsigned btn, bool v) override;

private:
    enum { B_FRONT = 0, B_RANGE = 1, B_DISK = 2 };
    PluginProcessor& processor_;
    ScreenImage screen_;
    ssp::ValueButton frontBtn_, rangeBtn_, diskBtn_;
    bool rangePage_ = false;
    juce::String message_;  // the last disk sync's result, shown for a while
    juce::uint32 messageUntil_ = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};

// compact editor for rack: the screen, with the main page's encoders
class PluginMiniEditor : public ssp::BaseView {
public:
    explicit PluginMiniEditor(PluginProcessor&);
    ~PluginMiniEditor() override = default;
    void drawView(Graphics&) override;

protected:
    void onSSPTimer() override;
    void onEncoder(unsigned enc, float v) override;

private:
    PluginProcessor& processor_;
    ScreenImage screen_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginMiniEditor)
};
