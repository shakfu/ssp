#pragma once

#include "PluginProcessor.h"
#include "ssp/controls/ValueButton.h"
#include "ssp/controls/VuMeter.h"
#include "ssp/editors/BaseViewEditor.h"
#include "ssp/editors/FileBrowser.h"
#include "ssp/editors/TextEdit.h"
#include "softcut/FadeCurves.h"

class VoiceView;
class GlobalView;

class PluginEditor : public ssp::BaseViewEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;
    void drawView(Graphics&) override;
    void resized() override;

protected:
    using base_type = ssp::BaseViewEditor;

    void onLeftButton(bool v) override;
    void onRightButton(bool v) override;
    void onButton(unsigned int id, bool v) override;
    void eventUp(bool longPress) override;
    void eventDown(bool longPress) override;
    void eventRightShift(bool longPress) override;
    void setView(unsigned newView) override;

private:
    static constexpr unsigned VOICES = PluginProcessor::VOICES;
    // views 0..VOICES-1 are the voices; VCE+ past the last track reaches the global view
    static constexpr unsigned V_GLOBAL = VOICES;
    static constexpr unsigned V_FILE = VOICES + 1;
    static constexpr unsigned V_SAVE = VOICES + 2;

    // Voice views use buttons 1-4, 6 and 7; Load is 5, Save 8. The browser and the name editor
    // replace the voice view, so Cancel can take 7. TextEdit uses 1 and 2.
    enum { B_LOAD = 4, B_CANCEL = 6, B_SAVE = 7 };

    void onLoadButton(bool v);
    void onSaveButton(bool v);
    void onCancelButton(bool v);
    bool fileMode() const { return view_ == int(V_FILE); }
    bool saveMode() const { return view_ == int(V_SAVE); }
    bool voiceMode() const { return view_ < int(VOICES); }
    bool globalMode() const { return view_ == int(V_GLOBAL); }
    // the voice's buffer, or its track's two for a linked pair
    unsigned saveMask() const;
    void showVoice(unsigned v);
    void drawFades(Graphics& g, unsigned v, int top, float len);

    unsigned voice_ = 0;
    std::vector<std::shared_ptr<VoiceView>> voiceViews_;
    std::shared_ptr<GlobalView> globalView_;
    // the curves every voice records with: sfct never changes their shapes from init(true)'s
    softcut::FadeCurves fadeCurves_;
    std::shared_ptr<ssp::FileBrowser> fileBrowser_;
    std::shared_ptr<ssp::TextEdit> saveEditor_;
    ssp::ValueButton loadBtn_, saveBtn_, cancelBtn_;
    ssp::StereoVuMeter inVu_;
    ssp::StereoVuMeter outVu_;

    PluginProcessor& processor_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
