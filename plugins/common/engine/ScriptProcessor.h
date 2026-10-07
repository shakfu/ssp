#pragma once

#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"
#include "engine/ScriptEngine.h"

namespace ssp::engine {

// Hosts a ScriptEngine: eight audio inputs and outputs, controls p1..p16, and the program file in
// the plugin state.
class ScriptProcessor : public EngineProcessor {
public:
    static constexpr int CHANNELS = ScriptEngine::CHANNELS;
    static constexpr int PARAMS = ScriptEngine::PARAMS;

    // programDir: where the browser opens until a program is loaded
    ScriptProcessor(std::unique_ptr<ScriptEngine> engine, const juce::String& programDir);

    juce::AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    ScriptEngine& script() { return static_cast<ScriptEngine&>(engine()); }
    juce::RangedAudioParameter& param(int i) { return *params_[size_t(i)]; }
    void loadProgram(const juce::String& path) { script().load(path.toStdString()); }
    juce::String programDir() const { return programDir_; }

    // The controls the running program declares, four to a page; all of them if it declares none.
    std::vector<ParamPage> pages();

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(juce::XmlElement*) override;
    void customToXml(juce::XmlElement*) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<juce::RangedAudioParameter*> params_;
    juce::String programDir_;
};

// Shows the program and its compile error; Load picks a program file.
class ScriptEditor : public EngineEditor {
public:
    explicit ScriptEditor(ScriptProcessor& p);

    // UI thread, each frame: rebuilds the pages when a load changed the declared controls
    void onSSPTimer() override;

protected:
    void drawStatus(juce::Graphics& g, juce::Rectangle<int> area) override;
    void loaded(const juce::String& file, const juce::String& dir) override;
    juce::String browseFrom() override;

private:
    ScriptProcessor& processor_;
    unsigned specsGen_;
};

// Compact editor for rack, with the same pages.
class ScriptMiniEditor : public EngineMiniEditor {
public:
    explicit ScriptMiniEditor(ScriptProcessor& p);
    void onSSPTimer() override;

private:
    ScriptProcessor& processor_;
    unsigned specsGen_;
};

}  // namespace ssp::engine
