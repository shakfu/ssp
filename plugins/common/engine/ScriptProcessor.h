#pragma once

#include "engine/EngineEditor.h"
#include "engine/EngineProcessor.h"
#include "engine/ScriptEngine.h"
#include "keys/KeyQueue.h"
#include "keys/TextBuffer.h"

namespace ssp::engine {

// The keys a plugin's code editor reads (ssp::KeyInput); port 0 for no editor.
struct CodeKeys {
    uint16_t port = 0;
    const char* env = nullptr;  // the variable whose value 0 turns the USB keyboard off
    const char* ext = nullptr;  // the built-in's file when saved: programDir/live<ext>
    const char* comment = "//";  // a line comment's start, which Ctrl-E's block skips
};

// Hosts a ScriptEngine: eight audio inputs and outputs, controls p1..p16, and the program file in
// the plugin state. With CodeKeys, also the program as text, edited from a keyboard and run in place
// of its file; the state then keeps the code run and the text.
class ScriptProcessor : public EngineProcessor {
public:
    static constexpr int CHANNELS = ScriptEngine::CHANNELS;
    static constexpr int PARAMS = ScriptEngine::PARAMS;

    // programDir: where the browser opens until a program is loaded
    ScriptProcessor(std::unique_ptr<ScriptEngine> engine, const juce::String& programDir, CodeKeys keys = {});
    ~ScriptProcessor() override;

    juce::AudioProcessorEditor* createEditor() override;
    static BusesProperties getBusesProperties();

    ScriptEngine& script() { return static_cast<ScriptEngine&>(engine()); }
    juce::RangedAudioParameter& param(int i) { return *params_[size_t(i)]; }
    // Message thread: also puts the program's text in text()
    void loadProgram(const juce::String& path);
    juce::String programDir() const { return programDir_; }

    bool hasKeys() const { return keys_.port != 0; }
    const CodeKeys& codeKeys() const { return keys_; }
    ssp::KeyQueue& keys() { return keyQueue_; }
    ssp::TextBuffer& text() { return text_; }  // message thread
    // Message thread: Ctrl-Enter runs text() as codePath(); Ctrl-S writes it there, and returns a line
    // for the status.
    void runCode();
    juce::String saveCode();
    // Ctrl-E: queues the block around the cursor to run beside the program; false if there is none
    bool runBlock(int& first, int& last);
    // the program's file, or programDir/live<ext> for the built-in
    juce::String codePath();

    // The controls the running program declares, four to a page; all of them if it declares none.
    std::vector<ParamPage> pages();
    // Message thread: sets each control the running program gives a default to that default.
    void applyDefaults();

protected:
    void control(const float* const* in, int n) override;
    void customFromXml(juce::XmlElement*) override;
    void customToXml(juce::XmlElement*) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::string programText();  // the program's code, file or built-in, as text() shows it
    void showProgram();         // programText() into text()

    std::vector<juce::RangedAudioParameter*> params_;
    juce::String programDir_;
    const CodeKeys keys_;
    ssp::KeyQueue keyQueue_;
    ssp::TextBuffer text_;
};

// Shows the program and its compile error; Load picks a program file. With CodeKeys, a Code view
// shows the program as text: the first key typed opens it, Esc or button 6 returns to the pages.
class ScriptEditor : public EngineEditor {
public:
    explicit ScriptEditor(ScriptProcessor& p);

    // UI thread, each frame: rebuilds the pages when a load changed the declared controls
    void onSSPTimer() override;

protected:
    void drawStatus(juce::Graphics& g, juce::Rectangle<int> area) override;
    void loaded(const juce::String& file, const juce::String& dir) override;
    juce::String browseFrom() override;
    void onButton(unsigned id, bool v) override;
    void setView(unsigned newView) override;  // Code is hidden in the browser, whose button 6 it is

private:
    enum { B_CODE = 5 };
    class CodeView;
    void takeKeys();
    bool chord(uint8_t usage);  // a Ctrl chord the editor handles; false to pass it to the text

    ScriptProcessor& processor_;
    unsigned specsGen_;
    bool loading_ = false;  // a program chosen with Load: its defaults apply once it compiles
    std::shared_ptr<CodeView> code_;
    unsigned codeView_ = 0;
    unsigned noteSeq_;
    ssp::ValueButton codeBtn_;
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
