#include "EngineEditor.h"

#include "ssp/controls/MiniControl.h"
#include "ssp/controls/ParamControl.h"
#include "ssp/editors/BarParamEditor.h"

namespace ssp::engine {

class EngineEditor::PageView : public ssp::BarParamEditor {
public:
    explicit PageView(EngineProcessor& p) : ssp::BarParamEditor(&p, false) {}
    unsigned page() const { return paramPage_; }
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
    buttons_.resize(4, nullptr);
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
    auto view = std::make_shared<PageView>(processor_);
    for (auto& pg : pages_) {
        std::shared_ptr<ssp::BaseParamControl> c[4];
        for (int i = 0; i < 4; i++)
            if (pg.c[i].param)
                c[i] = std::make_shared<ssp::BarParamControl>(*pg.c[i].param, pg.c[i].coarse, pg.c[i].fine, pg.colour);
        view->addParamPage(c[0], c[1], c[2], c[3]);
    }
    view->addButtonPage(button(buttons_[0], 24, juce::Colours::cyan), button(buttons_[1], 24, juce::Colours::cyan),
                        button(buttons_[2], 24, juce::Colours::cyan), button(buttons_[3], 24, juce::Colours::cyan),
                        nullptr, nullptr, nullptr, nullptr);
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
}

void EngineEditor::eventRight(bool longPress) {
    if (fileMode()) base_type::eventRight(longPress);
}

EngineMiniEditor::EngineMiniEditor(EngineProcessor& p, const std::vector<ParamPage>& pages,
                                   const std::vector<juce::RangedAudioParameter*>& buttons)
    : PageMiniView(&p) {
    static constexpr unsigned fh = 12 * COMPACT_UI_SCALE;
    std::vector<juce::RangedAudioParameter*> b = buttons;
    b.resize(4, nullptr);
    addButtonPage(button(b[0], fh, juce::Colours::cyan), button(b[1], fh, juce::Colours::cyan),
                  button(b[2], fh, juce::Colours::cyan), button(b[3], fh, juce::Colours::cyan), nullptr, nullptr,
                  nullptr, nullptr);
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
