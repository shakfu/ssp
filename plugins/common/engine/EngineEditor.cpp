#include "EngineEditor.h"

#include "ssp/controls/MiniControl.h"
#include "ssp/controls/ParamControl.h"
#include "ssp/editors/BarParamEditor.h"

namespace ssp::engine {

// BarParamEditor stacks every page as a row; the screen holds 6. This shows the 6 rows around the
// active page, so Up and Down reach every page.
class EngineEditor::PageView : public ssp::BarParamEditor {
public:
    static constexpr unsigned ROWS = 6;

    PageView(EngineProcessor& p, std::vector<int> groups) : ssp::BarParamEditor(&p, false), groups_(std::move(groups)) {}
    unsigned page() const { return paramPage_; }

    void eventUp(bool longPress) override {
        if (longPress) goTo(groupStart(-1));
        else BarParamEditor::eventUp(longPress);
        layout();
    }
    void eventDown(bool longPress) override {
        if (longPress) goTo(groupStart(1));
        else BarParamEditor::eventDown(longPress);
        layout();
    }

    // the global pages, remembering where we were
    void toGlobals() {
        if (groups_.empty() || groups_[paramPage_] == groups_.back()) return;
        back_ = paramPage_;
        for (unsigned pg = 0; pg < groups_.size(); pg++)
            if (groups_[pg] == groups_.back()) {
                goTo(pg);
                break;
            }
        layout();
    }
    void fromGlobals() {
        goTo(back_);
        layout();
    }

    void layout() {
        if (paramPage_ < top_) top_ = paramPage_;
        if (paramPage_ >= top_ + ROWS) top_ = paramPage_ - ROWS + 1;
        for (unsigned pg = 0; pg < controlPages_.size(); pg++) {
            bool shown = pg >= top_ && pg < top_ + ROWS;
            for (unsigned i = 0; i < 4; i++) {
                auto& c = controlPages_[pg].control_[i];
                if (!c) continue;
                if (shown) setParamBounds(pg - top_, i, c);
                c->setVisible(shown);
            }
        }
    }

private:
    // first page of the group `dir` groups away, wrapping
    unsigned groupStart(int dir) const {
        if (groups_.empty()) return paramPage_;
        std::vector<unsigned> starts;
        for (unsigned pg = 0; pg < groups_.size(); pg++)
            if (pg == 0 || groups_[pg] != groups_[pg - 1]) starts.push_back(pg);
        int at = 0;
        for (unsigned i = 0; i < starts.size(); i++)
            if (starts[i] <= paramPage_) at = int(i);
        int n = int(starts.size());
        return starts[size_t(((at + dir) % n + n) % n)];
    }
    void goTo(unsigned pg) {
        while (paramPage_ < pg) chgParamPage(1, false);
        while (paramPage_ > pg) chgParamPage(-1, false);
    }

    std::vector<int> groups_;
    unsigned top_ = 0;   // the first page shown
    unsigned back_ = 0;  // where a long Left returns to
};

static std::shared_ptr<ssp::ParamButton> button(juce::RangedAudioParameter* p, unsigned fh, juce::Colour clr) {
    return p ? std::make_shared<ssp::ParamButton>(*p, fh, clr, juce::Colours::black, true) : nullptr;
}

EngineEditor::EngineEditor(EngineProcessor& p, std::vector<ParamPage> pages,
                           std::vector<juce::RangedAudioParameter*> buttons, const juce::String& browseDir)
    : base_type(&p),
      processor_(p),
      pages_(std::move(pages)),
      buttons_(std::move(buttons)),
      loadBtn_("Load", [&](bool b) { onLoadButton(b); }, 24, juce::Colours::cyan),
      cancelBtn_("Cancel", [&](bool b) { onCancelButton(b); }, 24, juce::Colours::white) {
    buttons_.resize(8, nullptr);
    buttons_[B_LOAD] = nullptr;
    main_ = makePageView();
    addView(main_);

    juce::String dir = browseDir;
#ifdef __APPLE__
    dir = juce::File::getCurrentWorkingDirectory().getFullPathName();
#endif
    browser_ = std::make_shared<ssp::FileBrowser>(&p, dir);
    addView(browser_);

    setButtonBounds(loadBtn_, B_LOAD / 4, B_LOAD % 4);
    addAndMakeVisible(loadBtn_);
    setButtonBounds(cancelBtn_, B_CANCEL / 4, B_CANCEL % 4);
    addChildComponent(cancelBtn_);

    setView(V_MAIN);
    setSize(1600, 480);
}

std::shared_ptr<EngineEditor::PageView> EngineEditor::makePageView() {
    std::vector<int> groups;
    for (auto& pg : pages_) groups.push_back(pg.group);
    auto view = std::make_shared<PageView>(processor_, groups);
    for (auto& pg : pages_) {
        std::shared_ptr<ssp::BaseParamControl> c[4];
        for (int i = 0; i < 4; i++)
            if (pg.c[i].param)
                c[i] = std::make_shared<ssp::BarParamControl>(*pg.c[i].param, pg.c[i].coarse, pg.c[i].fine, pg.colour);
        view->addParamPage(c[0], c[1], c[2], c[3]);
    }
    view->layout();
    std::shared_ptr<ssp::ParamButton> b[8];
    for (int i = 0; i < 8; i++) b[i] = button(buttons_[size_t(i)], 24, juce::Colours::cyan);
    view->addButtonPage(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    return view;
}

void EngineEditor::setPages(std::vector<ParamPage> pages) {
    pages_ = std::move(pages);
    auto view = makePageView();
    removeChildComponent(main_.get());
    main_ = view;
    views_[V_MAIN] = view;
    view->setBounds(0, 0, 1600, 480);
    addChildComponent(view.get());
    if (view_ == V_MAIN) {
        view->setVisible(true);
        view->editorShown();
    }
}

void EngineEditor::setView(unsigned newView) {
    base_type::setView(newView);
    for (auto* c : std::initializer_list<juce::Component*>{ &leftBtn_, &rightBtn_, &upBtn_, &downBtn_ })
        c->setVisible(!fileMode());
    cancelBtn_.setVisible(fileMode());
}

void EngineEditor::drawView(juce::Graphics& g) {
    base_type::drawView(g);
    if (fileMode()) return;
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 20, juce::Font::plain)));
    unsigned pg = main_->page();
    if (pg < pages_.size()) {
        g.setColour(pages_[pg].colour);
        g.drawText(juce::String(pg + 1) + "/" + juce::String(pages_.size()) + " " + pages_[pg].name, panelX, 8, 330,
                   30, juce::Justification::left);
    }
    g.setColour(juce::Colours::white);
    g.drawText(juce::String::formatted("DSP %4.1f%% pk %4.1f%%", processor_.loadAverage() * 100.0f,
                                       processor_.loadPeak() * 100.0f),
               panelX + 330, 8, 1590 - panelX - 330, 30, juce::Justification::right);
    drawStatus(g, { panelX, 45, 1590 - panelX, 320 });
}

void EngineEditor::onLoadButton(bool v) {
    if (loadBtn_.value() == v) return;
    loadBtn_.onButton(v);
    if (v) return;
    if (!fileMode()) {
        juce::String from = browseFrom();
        if (from.isNotEmpty()) browser_->setFile(from);
        setView(V_FILE);
        return;
    }
    juce::String sel = browser_->selectedFile();
    loaded(sel, sel.isEmpty() ? browser_->baseDir() : juce::String());
    setView(V_MAIN);
}

void EngineEditor::onCancelButton(bool v) {
    if (cancelBtn_.value() == v) return;
    cancelBtn_.onButton(v);
    if (!v && fileMode()) setView(V_MAIN);
}

void EngineEditor::onButton(unsigned id, bool v) {
    switch (id) {
        case B_LOAD: onLoadButton(v); return;
        case B_CANCEL:
            if (fileMode()) {
                onCancelButton(v);
                return;
            }
            break;
        default:;
    }
    base_type::onButton(id, v);
}

// BaseViewEditor steps through the views on left/right; here they move only within the browser
void EngineEditor::onLeftButton(bool v) {
    if (fileMode()) views_[view_]->onLeftButton(v);
    else leftBtn_.onButton(v);
}

void EngineEditor::onRightButton(bool v) {
    if (fileMode()) views_[view_]->onRightButton(v);
    else rightBtn_.onButton(v);
}

void EngineEditor::eventLeft(bool longPress) {
    if (fileMode()) base_type::eventLeft(longPress);
    else if (longPress) main_->fromGlobals();
}

void EngineEditor::eventRight(bool longPress) {
    if (fileMode()) base_type::eventRight(longPress);
    else if (longPress) main_->toGlobals();
}

EngineMiniEditor::EngineMiniEditor(EngineProcessor& p, const std::vector<ParamPage>& pages,
                                   const std::vector<juce::RangedAudioParameter*>& buttons)
    : PageMiniView(&p) {
    static constexpr unsigned fh = 12 * COMPACT_UI_SCALE;
    std::vector<juce::RangedAudioParameter*> p8 = buttons;
    p8.resize(8, nullptr);
    std::shared_ptr<ssp::ParamButton> b[8];
    for (int i = 0; i < 8; i++) b[i] = button(p8[size_t(i)], fh, juce::Colours::cyan);
    addButtonPage(b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    setPages(pages);
}

void EngineMiniEditor::setPages(const std::vector<ParamPage>& pages) {
    clearParamPages();
    for (auto& pg : pages) {
        std::shared_ptr<ssp::BaseParamControl> c[4];
        for (int i = 0; i < 4; i++)
            if (pg.c[i].param) c[i] = std::make_shared<ssp::MiniControl>(*pg.c[i].param, pg.c[i].coarse, pg.c[i].fine);
        addParamPage(c[0], c[1], c[2], c[3], pg.colour);
    }
}

}  // namespace ssp::engine
