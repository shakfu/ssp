#include "ScriptProcessor.h"

#include "keys/CodeText.h"
#include "keys/Highlight.h"
#include "keys/KeyInput.h"
#include "ssp/EditorHost.h"

namespace ssp::engine {

static juce::Font mono(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

// p1..p16: named, scaled and given a unit by the running program's declaration, and moved by its CV
class ScriptParameter : public ssp::BaseFloatParameter, public ssp::ModulatedParameter {
public:
    explicit ScriptParameter(int i)
        : BaseFloatParameter(ScriptEngine::paramName(i), juce::String(ScriptEngine::paramName(i)).toUpperCase(), 0.0f,
                             1.0f, 0.0f),
          index_(i) {}

    void attach(const ScriptEngine* e) { engine_ = e; }

    juce::String getName(int maxLength) const override {
        auto sp = spec();
        return sp.label.empty() ? BaseFloatParameter::getName(maxLength)
                                : juce::String(sp.label).substring(0, maxLength);
    }
    juce::String getLabel() const override { return spec().unit; }
    float modulatedValue() const override {
        auto sp = spec();
        return engine_ && sp.cv >= 0 ? sp.unmap(engine_->value(index_)) : -1.0f;
    }
    juce::String getText(float normalised, int) const override {
        float v = spec().map(normalised);
        float a = std::fabs(v);
        return juce::String(v, a >= 100.0f ? 0 : a >= 10.0f ? 1 : a >= 1.0f ? 2 : 3);
    }

private:
    ScriptEngine::ParamSpec spec() const { return engine_ ? engine_->spec(index_) : ScriptEngine::ParamSpec(); }
    const int index_;
    const ScriptEngine* engine_ = nullptr;
};

ScriptProcessor::ScriptProcessor(std::unique_ptr<ScriptEngine> engine, const juce::String& programDir,
                                 CodeKeys keys, const BusesProperties& buses)
    : EngineProcessor(buses, createParameterLayout(), std::move(engine)),
      programDir_(programDir),
      keys_(keys) {
    init();
    for (int i = 0; i < PARAMS; i++) {
        auto* p = vts().getParameter(ScriptEngine::paramName(i));
        static_cast<ScriptParameter*>(p)->attach(&script());
        params_.push_back(p);
    }
    if (!hasKeys()) return;
    ssp::KeyInput::acquire(keys_.port, keys_.env);
    ssp::KeyInput::setTarget(&keyQueue_);  // the newest instance takes the keys
    showProgram();
}

ScriptProcessor::~ScriptProcessor() {
    if (!hasKeys()) return;
    ssp::KeyInput::clearTarget(&keyQueue_);
    ssp::KeyInput::release();
}

void ScriptProcessor::loadProgram(const juce::String& path) {
    script().load(path.toStdString());
    showProgram();
}

std::string ScriptProcessor::programText() {
    std::string code, path = script().path();
    if (!script().code(code)) {
        if (path.empty()) code = script().builtin();
        else if (!ScriptEngine::readText(path, code)) code.clear();
    }
    while (!code.empty() && code.back() == '\n') code.pop_back();
    return code;
}

void ScriptProcessor::showProgram() {
    if (hasKeys()) text_.setText(programText());
}

juce::String ScriptProcessor::codePath() {
    juce::String path = script().path();
    return path.isNotEmpty() ? path : programDir_ + "/live" + keys_.ext;
}

void ScriptProcessor::runCode() {
    script().loadCode(text_.text() + "\n", codePath().toStdString());
}

void ScriptProcessor::clearText() {
    std::string now = text_.text();
    if (now.empty()) {
        text_.setText(cleared_);
        return;
    }
    cleared_ = now;
    text_.setText("");
}

bool ScriptProcessor::resetCode() {
    if (!script().canReset()) return false;
    if (!text_.text().empty()) cleared_ = text_.text();
    text_.setText("");
    script().loadCode("\n", "");
    return true;
}

bool ScriptProcessor::runBlock(int& first, int& last) {
    if (!text_.block(first, last, keys_.comment)) return false;
    script().evaluate(text_.lines(first, last) + "\n", first > 0 ? text_.lines(0, first - 1) : std::string(),
                      codePath().toStdString());
    return true;
}

juce::String ScriptProcessor::saveCode() {
    juce::File f(codePath());
    f.getParentDirectory().createDirectory();
    return f.replaceWithText(text_.text() + "\n") ? "saved " + f.getFullPathName()
                                                  : "cannot write " + f.getFullPathName();
}

juce::AudioProcessorValueTreeState::ParameterLayout ScriptProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout params;
    for (int i = 0; i < PARAMS; i++) params.add(std::make_unique<ScriptParameter>(i));
    return params;
}

ScriptProcessor::BusesProperties ScriptProcessor::getBusesProperties() {
    BusesProperties props;
    for (int c = 0; c < CHANNELS; c++) props.addBus(true, "In " + juce::String(c + 1), juce::AudioChannelSet::mono());
    for (int c = 0; c < CHANNELS; c++) props.addBus(false, "Out " + juce::String(c + 1), juce::AudioChannelSet::mono());
    return props;
}

void ScriptProcessor::control(const float* const* in, int) {
    for (int i = 0; i < PARAMS; i++) script().setParam(i, params_[size_t(i)]->getValue());
    script().readCv(in);
}

void ScriptProcessor::customToXml(juce::XmlElement* xml) {
    xml->setAttribute("program", juce::String(script().path()));
    std::string code;
    if (script().code(code)) xml->setAttribute("code", juce::String(code));
    if (hasKeys() && text_.text() != programText()) xml->setAttribute("text", juce::String(text_.text()));
}

// code: the program last run from the editor; text: the editor's, when it differs from the program
void ScriptProcessor::customFromXml(juce::XmlElement* xml) {
    std::string program = xml->getStringAttribute("program").toStdString();
    if (xml->hasAttribute("code")) script().loadCode(xml->getStringAttribute("code").toStdString(), program);
    else script().load(program);
    if (hasKeys() && xml->hasAttribute("text")) text_.setText(xml->getStringAttribute("text").toStdString());
    else showProgram();
}

std::vector<ParamPage> ScriptProcessor::pages() {
    std::vector<int> shown;
    for (int i = 0; i < PARAMS; i++)
        if (!script().spec(i).label.empty()) shown.push_back(i);
    bool declared = !shown.empty();
    if (!declared)
        for (int i = 0; i < PARAMS; i++) shown.push_back(i);
    std::vector<ParamPage> pages;
    for (size_t at = 0; at < shown.size(); at += 4) {
        ParamPage page;
        page.name = declared ? juce::String("controls")
                             : "p" + juce::String(shown[at] + 1) + "-p" + juce::String(shown[at] + 4);
        for (size_t k = 0; k < 4 && at + k < shown.size(); k++) page.c[k] = { params_[size_t(shown[at + k])], 0.05f, 0.005f };
        pages.push_back(page);
    }
    return pages;
}

void ScriptProcessor::applyDefaults() {
    for (int i = 0; i < PARAMS; i++) {
        float def = script().spec(i).def;
        if (def >= 0.0f) params_[size_t(i)]->setValueNotifyingHost(def);
    }
}

juce::AudioProcessorEditor* ScriptProcessor::createEditor() {
    if (useCompactUI()) return new ssp::EditorHost(this, new ScriptMiniEditor(*this), true);
    return new ssp::EditorHost(this, new ScriptEditor(*this), false);
}

// The program as text, above a status line and the last compile error; the button row stays below.
class ScriptEditor::CodeView : public ssp::BaseEditor {
public:
    static constexpr int H = 376;

    explicit CodeView(ScriptProcessor& p) : BaseEditor(&p), processor_(p) {
        const auto& keys = p.codeKeys();
        if (keys.language && keys.highlights)
            highlighter_ = std::make_unique<ssp::Highlighter>(keys.language(), keys.highlights, keys.lastWins);
    }

    // a line shown for a moment in place of the error
    void note(const juce::String& s) {
        note_ = s;
        noteUntil_ = juce::Time::getMillisecondCounter() + 3000;
    }
    // rows a Ctrl-E sent, highlighted for a moment
    void flash(int first, int last) {
        flashFirst_ = first, flashLast_ = last;
        flashUntil_ = juce::Time::getMillisecondCounter() + 300;
    }

    // as the arrow keys: 1 rows, 2 columns
    void onEncoder(unsigned enc, float v) override {
        if (enc == 0) processor_.text().key(v > 0 ? 0x51 : 0x52, 0);
        else if (enc == 1) processor_.text().key(v > 0 ? 0x4F : 0x50, 0);
    }

    static juce::String runState(const ScriptEngine& script) {
        std::string s = script.runState();
        return s.empty() ? juce::String("running") : juce::String(s);
    }

    void drawView(juce::Graphics& g) override {
        constexpr int W = 1600, MARGIN = 10, LINE = 26, ERROR_Y = H - 2 * LINE, STATUS_Y = ERROR_Y - LINE;
        const juce::Colour amber(255, 214, 140), dim(150, 150, 150);
        g.setColour(juce::Colours::black);
        g.fillRect(0, 0, W, H);
        auto& script = processor_.script();
        const juce::String parts = script.parts();
        const int codeBottom = parts.isEmpty() ? STATUS_Y : STATUS_Y - LINE;  // parts take a row
        const bool flashing = juce::Time::getMillisecondCounter() < flashUntil_;
        // The cursor's line is on screen, and a line takes at least one screen row, so every line shown
        // is within a screen's rows of it; only those are queried
        const int rows = (codeBottom - 12) / LINE + 1, row = processor_.text().row();
        if (highlighter_) highlighter_->update(processor_.text(), row - rows, row + rows);
        g.setFont(mono(22));
        ssp::drawCode(g, processor_.text(), { MARGIN, 4, W - 2 * MARGIN, codeBottom - 8 }, LINE, top_,
                      flashing ? flashFirst_ : 0, flashing ? flashLast_ : -1,
                      highlighter_ ? &highlighter_->kinds() : nullptr);
        g.setColour(juce::Colour(60, 60, 60));
        g.drawHorizontalLine(codeBottom - 3, 0.f, float(W));
        if (parts.isNotEmpty()) {
            g.setColour(dim);
            g.drawText(parts, MARGIN, codeBottom, W - 2 * MARGIN, LINE, juce::Justification::left, true);
        }

        const juce::String path = script.path(), error = script.error();
        const auto& keys = processor_.codeKeys();
        juce::String status =
            (path.isEmpty() ? juce::String("built-in") : juce::File(path).getFileName()) + ":" +
            juce::String(processor_.text().row() + 1) + "   " +
            (script.loading() ? "compiling" : !error.isEmpty() ? "error" : runState(script)) + "   dsp " +
            juce::String(int(processor_.loadAverage() * 100.0f)) + "%   keys " +
            (ssp::KeyInput::keyboardConnected() ? juce::String("usb") : "tcp :" + juce::String(keys.port)) +
            "   " + (keys.hint ? keys.hint : CodeKeys::DEFAULT_HINT);
        g.setColour(amber);
        g.drawText(status, MARGIN, STATUS_Y, W - 2 * MARGIN, LINE, juce::Justification::left, false);

        const bool noting = juce::Time::getMillisecondCounter() < noteUntil_;
        // a highlights query that fails to compile is a build error; show it rather than hide it
        const juce::String shown = noting ? note_ : error.isNotEmpty() || !highlighter_ ? error
                                                                       : juce::String(highlighter_->error());
        g.setColour(noting ? dim : juce::Colours::orange);
        g.setFont(mono(18));
        g.drawFittedText(shown, MARGIN, ERROR_Y, W - 2 * MARGIN, 2 * LINE, juce::Justification::topLeft, 2, 1.0f);
    }

private:
    ScriptProcessor& processor_;
    int top_ = 0;  // the first screen row shown
    juce::String note_;
    juce::uint32 noteUntil_ = 0;
    int flashFirst_ = 0, flashLast_ = -1;
    juce::uint32 flashUntil_ = 0;
    std::unique_ptr<ssp::Highlighter> highlighter_;
};

ScriptEditor::ScriptEditor(ScriptProcessor& p)
    : EngineEditor(p, p.pages(), {}, p.programDir()),
      processor_(p),
      specsGen_(p.script().specsGeneration()),
      noteSeq_(p.script().noteSeq()),
      codeBtn_("Code", [](bool) {}, 24, juce::Colours::orange) {
    if (!p.hasKeys()) return;
    code_ = std::make_shared<CodeView>(p);
    codeView_ = addView(code_);
    code_->setBounds(0, 0, 1600, CodeView::H);  // the buttons below stay in view
    setButtonBounds(codeBtn_, B_CODE / 4, B_CODE % 4);
    addAndMakeVisible(codeBtn_);
}

void ScriptEditor::onButton(unsigned id, bool v) {
    if (id != B_CODE || !code_ || view_ == V_FILE) {
        EngineEditor::onButton(id, v);
        return;
    }
    if (codeBtn_.value() == v) return;
    codeBtn_.onButton(v);
    if (!v) setView(view_ == int(codeView_) ? unsigned(V_MAIN) : codeView_);
}

void ScriptEditor::setView(unsigned newView) {
    EngineEditor::setView(newView);
    codeBtn_.setVisible(code_ && newView != V_FILE);
}

// Keys into the code, and the actions they ask for. The browser drops them; on the pages, a key
// other than Esc opens the code.
void ScriptEditor::takeKeys() {
    constexpr uint8_t ESC = 0x29;
    ssp::KeyInput::setTarget(&processor_.keys());  // also marks this editor as on screen
    for (const auto& k : processor_.keys().take(ssp::KeyQueue::Clock::now())) {
        if (view_ == V_FILE) continue;
        if (view_ != int(codeView_)) {
            if (k.usage == ESC) continue;
            setView(codeView_);
        }
        if ((k.mods & ssp::TextBuffer::CTRL) && chord(k.usage)) continue;
        switch (processor_.text().key(k.usage, k.mods)) {
            case ssp::TextBuffer::EVALUATE: processor_.runCode(); break;
            case ssp::TextBuffer::SAVE: code_->note(processor_.saveCode()); break;
            case ssp::TextBuffer::OPEN: browse(); break;
            case ssp::TextBuffer::STOP: setView(V_MAIN); break;  // Esc
            default: break;
        }
    }
    unsigned seq = processor_.script().noteSeq();
    if (seq != noteSeq_) {
        noteSeq_ = seq;
        code_->note(processor_.script().note());
    }
}

// Ctrl-E runs the block; Ctrl-1..9 and Ctrl-0 remove parts 1..10, Ctrl-- the newest; Ctrl-. clears;
// Ctrl-L clears the text; Ctrl-R resets.
bool ScriptEditor::chord(uint8_t usage) {
    constexpr uint8_t E = 0x08, L = 0x0F, R = 0x15, ONE = 0x1E, ZERO = 0x27, MINUS = 0x2D, DOT = 0x37;
    auto& script = processor_.script();
    if (usage == E) {
        int first, last;
        if (processor_.runBlock(first, last)) code_->flash(first, last);
    } else if (usage >= ONE && usage <= ZERO) {
        script.removePart(usage - ONE + 1);
    } else if (usage == MINUS) {
        script.removePart(0);
    } else if (usage == DOT) {
        script.clearParts();
    } else if (usage == L) {
        bool had = !processor_.text().text().empty();
        processor_.clearText();
        code_->note(had ? "cleared: Ctrl-L again brings it back" : "restored");
    } else if (usage == R) {
        code_->note(processor_.resetCode() ? "reset: stopped, cleared, no program; Ctrl-L brings the text back"
                                       : "no reset in this module");
    } else {
        return false;
    }
    return true;
}

void ScriptEditor::onSSPTimer() {
    EngineEditor::onSSPTimer();
    if (code_) takeKeys();
    unsigned gen = processor_.script().specsGeneration();
    if (gen == specsGen_) return;
    specsGen_ = gen;
    if (loading_) processor_.applyDefaults();
    loading_ = false;
    setPages(processor_.pages());
}

ScriptMiniEditor::ScriptMiniEditor(ScriptProcessor& p)
    : EngineMiniEditor(p, p.pages(), {}), processor_(p), specsGen_(p.script().specsGeneration()) {
}

void ScriptMiniEditor::onSSPTimer() {
    EngineMiniEditor::onSSPTimer();
    unsigned gen = processor_.script().specsGeneration();
    if (gen == specsGen_) return;
    specsGen_ = gen;
    setPages(processor_.pages());
}

void ScriptEditor::loaded(const juce::String& file, const juce::String&) {
    if (file.isEmpty()) return;
    processor_.loadProgram(file);
    loading_ = true;
}

juce::String ScriptEditor::browseFrom() {
    juce::String path = processor_.script().path();
    return path.isNotEmpty() ? path : processor_.programDir();
}

void ScriptEditor::drawStatus(juce::Graphics& g, juce::Rectangle<int> area) {
    juce::String path = processor_.script().path();
    g.setColour(juce::Colours::white);
    g.drawText(path.isEmpty() ? juce::String("built-in") : juce::File(path).getFileName(), area.removeFromTop(30),
               juce::Justification::left);
    juce::String status = processor_.script().status();
    g.setColour(juce::Colours::grey);
    for (auto& line : juce::StringArray::fromLines(status))
        if (line.isNotEmpty()) g.drawText(line, area.removeFromTop(30), juce::Justification::left);
    juce::String err = processor_.script().error();
    if (err.isEmpty()) return;
    g.setColour(juce::Colours::orange);
    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 16, juce::Font::plain)));
    g.drawFittedText(err, area.reduced(0, 4), juce::Justification::topLeft, 12, 1.0f);
}

}  // namespace ssp::engine
