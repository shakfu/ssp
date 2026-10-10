#include "ScriptProcessor.h"

#include "ssp/EditorHost.h"

namespace ssp::engine {

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

ScriptProcessor::ScriptProcessor(std::unique_ptr<ScriptEngine> engine, const juce::String& programDir)
    : EngineProcessor(getBusesProperties(), createParameterLayout(), std::move(engine)), programDir_(programDir) {
    init();
    for (int i = 0; i < PARAMS; i++) {
        auto* p = vts().getParameter(ScriptEngine::paramName(i));
        static_cast<ScriptParameter*>(p)->attach(&script());
        params_.push_back(p);
    }
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
}

void ScriptProcessor::customFromXml(juce::XmlElement* xml) {
    loadProgram(xml->getStringAttribute("program"));
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

ScriptEditor::ScriptEditor(ScriptProcessor& p)
    : EngineEditor(p, p.pages(), {}, p.programDir()), processor_(p), specsGen_(p.script().specsGeneration()) {
}

void ScriptEditor::onSSPTimer() {
    EngineEditor::onSSPTimer();
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
