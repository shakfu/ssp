#include "PluginEditor.h"

#include "KeyInput.h"
#include "SSP.h"

namespace {

constexpr int W = ttyp::Teletype::SCREEN_W;
constexpr int H = ttyp::Teletype::SCREEN_H;
const juce::Colour pixelColour(255, 214, 140);  // amber on black

juce::Font mono(float h) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

}  // namespace

ScreenImage::ScreenImage() : image_(juce::Image::RGB, W, H, true) {
}

void ScreenImage::update(ttyp::Teletype& tt) {
    uint32_t seq = tt.screen(px_);
    if (seq == seq_) return;
    seq_ = seq;
    juce::Image::BitmapData bm(image_, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            bm.setPixelColour(x, y, juce::Colours::black.interpolatedWith(pixelColour, px_[y * W + x] / 15.0f));
}

void ScreenImage::draw(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);  // square pixels
    g.drawImage(image_, area.toFloat());
}

void onTeletypeEncoder(PluginProcessor& p, unsigned enc, float v, bool rangePage) {
    if (enc >= ttyp::Teletype::OUTS || (!rangePage && enc > 1)) return;
    auto* param = p.getParameter(rangePage ? ID::cvRange(enc) : enc == 0 ? ID::param : ID::in_range);
    if (!param) return;
    float step = !rangePage && enc == 0 ? 0.01f : 1.0f;
    float now = param->convertFrom0to1(param->getValue());
    param->setValueNotifyingHost(param->convertTo0to1(now + (v > 0 ? step : -step)));
}

void onTeletypeTimer(PluginProcessor& p, ScreenImage& screen) {
    if (auto* tt = p.teletype()) {
        ttyp::KeyInput::setTarget(tt);
        screen.update(*tt);
    }
}

PluginEditor::PluginEditor(PluginProcessor& p)
    : ssp::BaseEditor(&p),
      processor_(p),
      frontBtn_("Front", [](bool) {}, 24, juce::Colours::white),
      rangeBtn_("Ranges", [](bool) {}, 24, juce::Colours::cyan),
      diskBtn_("Disk", [](bool) {}, 24, juce::Colours::orange) {
    setButtonBounds(frontBtn_, B_FRONT / 4, B_FRONT % 4);
    setButtonBounds(rangeBtn_, B_RANGE / 4, B_RANGE % 4);
    setButtonBounds(diskBtn_, B_DISK / 4, B_DISK % 4);
    addAndMakeVisible(frontBtn_);
    addAndMakeVisible(rangeBtn_);
    addAndMakeVisible(diskBtn_);
}

void PluginEditor::onSSPTimer() {
    onTeletypeTimer(processor_, screen_);
}

void PluginEditor::onEncoder(unsigned enc, float v) {
    onTeletypeEncoder(processor_, enc, v, rangePage_);
}

void PluginEditor::onButton(unsigned btn, bool v) {
    if (btn == B_FRONT) {
        frontBtn_.onButton(v);
        if (auto* tt = processor_.teletype()) tt->setFront(v);
    } else if (btn == B_RANGE && v) {
        rangePage_ = !rangePage_;
        rangeBtn_.onButton(rangePage_);
    } else if (btn == B_DISK) {
        diskBtn_.onButton(v);
        if (!v) return;
        message_ = processor_.syncScenes();
        messageUntil_ = juce::Time::getMillisecondCounter() + 8000;
    }
}

void PluginEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    screen_.draw(g, { 20, 20, W * 6, H * 6 });

    constexpr int x = 900, w = 680, lh = 32;
    int y = 20;
    auto line = [&](const juce::String& s, juce::Colour c = juce::Colours::white) {
        g.setColour(c);
        g.drawText(s, x, y, w, lh, juce::Justification::left);
        y += lh;
    };
    g.setFont(mono(24));
    line(juce::String(JucePlugin_Name) + " " + JucePlugin_VersionString, juce::Colours::orange);
    auto* tt = processor_.teletype();
    if (!tt) {
        line("firmware: " + processor_.error(), juce::Colours::red);
        return;
    }
    line(ttyp::KeyInput::keyboardConnected() ? "keyboard: connected" : "keyboard: none (or tcp :" +
                                                                           juce::String(ttyp::KeyInput::PORT) + ")");
    auto text = [&](const juce::String& id) { return processor_.getParameter(id)->getCurrentValueAsText(); };
    auto active = rangePage_ ? juce::Colours::white : juce::Colours::grey;
    line((rangePage_ ? "enc 1-4: CV ranges" : "enc 1: param  enc 2: in range"), juce::Colours::cyan);
    line("param " + text(ID::param) + "   in " + text(ID::in_range), rangePage_ ? juce::Colours::grey : juce::Colours::white);
    y += lh / 2;
    for (unsigned o = 0; o < ttyp::Teletype::OUTS; o++) {
        line(juce::String::formatted("CV %u %6.3f V %-6s TR %u %s", o + 1, tt->cv(o),
                                     text(ID::cvRange(o)).toRawUTF8(), o + 1, tt->tr(o) ? "on" : "-"),
             active);
    }
    y += lh / 2;
    if (juce::Time::getMillisecondCounter() < messageUntil_)
        line(message_, juce::Colours::orange);
    else
        line("disk: " + PluginProcessor::sceneDir(), juce::Colours::grey);
}

PluginMiniEditor::PluginMiniEditor(PluginProcessor& p) : ssp::BaseView(&p, true), processor_(p) {
}

void PluginMiniEditor::onSSPTimer() {
    onTeletypeTimer(processor_, screen_);
}

void PluginMiniEditor::onEncoder(unsigned enc, float v) {
    onTeletypeEncoder(processor_, enc, v, false);
}

void PluginMiniEditor::drawView(Graphics& g) {
    g.fillAll(juce::Colours::black);
    constexpr int s = 4;
    const int x = (int(SSP_COMPACT_WIDTH) - W * s) / 2;
    screen_.draw(g, { x, 20 * int(COMPACT_UI_SCALE), W * s, H * s });
    if (!processor_.teletype()) {
        g.setColour(juce::Colours::red);
        g.setFont(mono(12 * COMPACT_UI_SCALE));
        g.drawText("firmware: " + processor_.error(), 0, H * s + 30 * int(COMPACT_UI_SCALE), SSP_COMPACT_WIDTH,
                   20 * COMPACT_UI_SCALE, juce::Justification::centred);
    }
}
