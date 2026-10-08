#pragma once

#include <vector>

#include "engine/EngineProcessor.h"
#include "ssp/controls/ParamButton.h"
#include "ssp/controls/ValueButton.h"
#include "ssp/editors/BaseMiniView.h"
#include "ssp/editors/BaseViewEditor.h"
#include "ssp/editors/FileBrowser.h"

namespace ssp::engine {

// Up to four parameters on one encoder page.
struct ParamPage {
    struct Control {
        juce::RangedAudioParameter* param = nullptr;
        float coarse = 0.1f, fine = 0.01f;
    };
    juce::String name;
    juce::Colour colour = juce::Colours::white;
    Control c[4];
    // Pages of a group are adjacent: a long Up or Down jumps between groups, a long Right to the
    // last group (the global pages) and a long Left back.
    int group = 0;
};

// Full-screen editor: the plugin's parameter pages, a status panel right of them, and a file browser
// behind a Load button.
class EngineEditor : public ssp::BaseViewEditor {
public:
    // buttons: parameters for buttons 1-8 (null for none). Button 5 is Load, so its entry is ignored;
    // button 7 is Cancel while the browser is open.
    EngineEditor(EngineProcessor& p, std::vector<ParamPage> pages,
                 std::vector<juce::RangedAudioParameter*> buttons, const juce::String& browseDir);

    // Replaces the parameter pages, back at the first.
    void setPages(std::vector<ParamPage> pages);

protected:
    // Status panel, below the page title, right of the parameter bars.
    virtual void drawStatus(juce::Graphics& g, juce::Rectangle<int> area) {}
    // Load pressed in the browser: the selected file, or empty with the directory being browsed.
    virtual void loaded(const juce::String& file, const juce::String& dir) {}
    // The file or directory the browser opens at; empty keeps its last place.
    virtual juce::String browseFrom() { return {}; }

    void drawView(juce::Graphics& g) override;
    void onButton(unsigned id, bool v) override;
    void onLeftButton(bool v) override;
    void onRightButton(bool v) override;
    void eventLeft(bool longPress) override;
    void eventRight(bool longPress) override;
    void setView(unsigned newView) override;

    static constexpr int panelX = 910;
    using base_type = ssp::BaseViewEditor;

private:
    enum { V_MAIN, V_FILE };
    enum { B_LOAD = 4, B_CANCEL = 6 };
    class PageView;

    bool fileMode() const { return view_ == V_FILE; }
    std::shared_ptr<PageView> makePageView();
    void onLoadButton(bool v);
    void onCancelButton(bool v);

    EngineProcessor& processor_;
    std::vector<ParamPage> pages_;
    std::vector<juce::RangedAudioParameter*> buttons_;
    std::shared_ptr<PageView> main_;
    std::shared_ptr<ssp::FileBrowser> browser_;
    ssp::ValueButton loadBtn_, cancelBtn_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineEditor)
};

// Compact editor for rack: the same pages and buttons, no browser.
class EngineMiniEditor : public ssp::PageMiniView {
public:
    EngineMiniEditor(EngineProcessor& p, const std::vector<ParamPage>& pages,
                     const std::vector<juce::RangedAudioParameter*>& buttons);
    void setPages(const std::vector<ParamPage>& pages);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineMiniEditor)
};

}  // namespace ssp::engine
