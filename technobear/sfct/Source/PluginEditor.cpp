#include "PluginEditor.h"

#include "PluginProcessor.h"
#include "ssp/controls/ParamButton.h"
#include "ssp/controls/ParamControl.h"
#include "ssp/editors/BarParamEditor.h"

using pcontrol_type = ssp::BarParamControl;
using bcontrol_type = ssp::ParamButton;

static constexpr unsigned VOICES = PluginProcessor::VOICES;
static constexpr unsigned TRACKS = sfct::TRACKS;

// track colours from softcut-demo; both voices of a track share one
static const juce::Colour trackClrs[TRACKS] = { juce::Colour(230, 90, 80), juce::Colour(80, 170, 230),
                                                juce::Colour(120, 200, 110), juce::Colour(220, 180, 60) };

class VoiceView : public ssp::BarParamEditor {
public:
    VoiceView(PluginProcessor& p, unsigned v) : ssp::BarParamEditor(&p, false) {
        auto& vp = p.getVoice(v);
        auto clr = trackClrs[v / 2];
        addParamPage(std::make_shared<pcontrol_type>(vp.rate, 0.05f, 0.001f, clr),
                     std::make_shared<pcontrol_type>(vp.start, 1.0f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.end, 1.0f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.level, 0.1f, 0.01f, clr));
        addParamPage(std::make_shared<pcontrol_type>(vp.rec_level, 0.1f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.pre_level, 0.1f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.in_gain, 0.1f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.pan, 0.1f, 0.01f, clr));
        addParamPage(std::make_shared<pcontrol_type>(vp.fade, 0.01f, 0.001f, clr),
                     std::make_shared<pcontrol_type>(vp.slew, 0.1f, 0.01f, clr),
                     std::make_shared<pcontrol_type>(vp.lpf, 500.0f, 10.0f, clr),
                     std::make_shared<pcontrol_type>(vp.lp_mix, 0.1f, 0.01f, clr));
        addButtonPage(std::make_shared<bcontrol_type>(vp.play, 24, clr),
                      std::make_shared<bcontrol_type>(vp.rec, 24, Colours::red),
                      std::make_shared<bcontrol_type>(vp.loop, 24, clr),
                      std::make_shared<bcontrol_type>(vp.cut, 24, clr, Colours::black, true),
                      nullptr,  // Load
                      std::make_shared<bcontrol_type>(vp.on, 24, clr),
                      std::make_shared<bcontrol_type>(vp.link, 24, Colours::white), nullptr);
    }

    unsigned page() const { return paramPage_; }
    unsigned pages() const { return unsigned(controlPages_.size()); }
    void goToPage(unsigned p) {
        while (paramPage_ < p && paramPage_ + 1 < pages()) chgParamPage(1, false);
        while (paramPage_ > p) chgParamPage(-1, false);
    }
};

class GlobalView : public ssp::BarParamEditor {
public:
    explicit GlobalView(PluginProcessor& p) : ssp::BarParamEditor(&p, false) {
        addParamPage(std::make_shared<pcontrol_type>(*p.getParameter(ID::mode), 1.0f, 1.0f, Colours::white), nullptr,
                     nullptr, nullptr);
        addButtonPage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    }
};

PluginEditor::PluginEditor(PluginProcessor& p)
    : base_type(&p),
      loadBtn_("Load", [&](bool b) { onLoadButton(b); }, 24, Colours::cyan),
      saveBtn_("Save", [&](bool b) { onSaveButton(b); }, 24, Colours::yellow),
      cancelBtn_("Cancel", [&](bool b) { onCancelButton(b); }, 24, Colours::white),
      processor_(p) {
    leftBtn_.label("TRK-");
    rightBtn_.label("TRK+");

    for (unsigned v = 0; v < VOICES; v++) {
        voiceViews_.push_back(std::make_shared<VoiceView>(p, v));
        addView(voiceViews_.back());
    }
    addView(std::make_shared<GlobalView>(p));

    String defDir = "/media/BOOT/samples";
#ifdef __APPLE__
    defDir = File::getCurrentWorkingDirectory().getFullPathName();
#endif
    fileBrowser_ = std::make_shared<ssp::FileBrowser>(&p, defDir);
    addView(fileBrowser_);
    saveEditor_ = std::make_shared<ssp::TextEdit>(&p);
    addView(saveEditor_);

    setButtonBounds(loadBtn_, B_LOAD / 4, B_LOAD % 4);
    addAndMakeVisible(loadBtn_);
    setButtonBounds(saveBtn_, B_SAVE / 4, B_SAVE % 4);
    addAndMakeVisible(saveBtn_);
    setButtonBounds(cancelBtn_, B_CANCEL / 4, B_CANCEL % 4);
    addChildComponent(cancelBtn_);

    inVu_.init("In");
    outVu_.init("Out");
    addAndMakeVisible(inVu_);
    addAndMakeVisible(outVu_);

    showVoice(voice_);
    setSize(1600, 480);
}

void PluginEditor::setView(unsigned newView) {
    base_type::setView(newView);
    bool main = voiceMode() || globalMode();
    for (auto* c : std::initializer_list<Component*>{ &leftBtn_, &rightBtn_, &upBtn_, &downBtn_, &inVu_, &outVu_ }) {
        c->setVisible(main);
    }
    loadBtn_.setVisible(voiceMode() || fileMode());
    saveBtn_.setVisible(voiceMode() || saveMode());
    cancelBtn_.setVisible(!main);
}

void PluginEditor::showVoice(unsigned v) {
    voice_ = v;
    processor_.setDisplayTrack(v / 2);
    setView(v);
}

unsigned PluginEditor::saveMask() const {
    auto& vp = processor_.getVoice(voice_);
    unsigned left = voice_ & ~1u;
    if (vp.link.getValue() > 0.5f) return (1u << processor_.bufferOf(left)) | (1u << processor_.bufferOf(left + 1));
    return 1u << processor_.bufferOf(voice_);
}

void PluginEditor::onSaveButton(bool v) {
    if (saveBtn_.value() == v) return;
    saveBtn_.onButton(v);
    if (v || !(voiceMode() || saveMode())) return;
    if (!saveMode()) {
        String name = "sfct-" + Time::getCurrentTime().formatted("%y%m%d-%H%M%S");
        saveEditor_->setText(name.toStdString());
        setView(V_SAVE);
        return;
    }
    String name = File::createLegalFileName(String(saveEditor_->getText()).trim());
    if (name.isNotEmpty()) {
        File dir(fileBrowser_->baseDir());
        String fn = processor_.getBufferFile(processor_.bufferOf(voice_));
        if (fn.isNotEmpty()) dir = File(fn).getParentDirectory();
        processor_.saveBuffers(dir.getChildFile(name).withFileExtension("wav").getFullPathName(), saveMask());
    }
    setView(voice_);
}

void PluginEditor::onLoadButton(bool v) {
    if (loadBtn_.value() == v) return;
    loadBtn_.onButton(v);
    if (v || !(voiceMode() || fileMode())) return;
    if (!fileMode()) {
        setView(V_FILE);
        String fn = processor_.getBufferFile(processor_.bufferOf(voice_));
        if (fn.isNotEmpty()) fileBrowser_->setFile(fn);
        return;
    }
    String sel = fileBrowser_->selectedFile();
    if (sel.isNotEmpty() && !File(sel).isDirectory()) processor_.loadFile(sel, voice_, true);
    setView(voice_);
}

void PluginEditor::onCancelButton(bool v) {
    if (cancelBtn_.value() == v) return;
    cancelBtn_.onButton(v);
    if (!v && (fileMode() || saveMode())) setView(voice_);
}

void PluginEditor::onButton(unsigned int id, bool v) {
    base_type::onButton(id, v);
    switch (id) {
        case B_LOAD: onLoadButton(v); break;
        case B_SAVE: onSaveButton(v); break;
        case B_CANCEL: onCancelButton(v); break;
        default:;
    }
}

// EN-/EN+ page through a stereo pair as one sequence: the left voice's pages, then the right's
void PluginEditor::eventUp(bool longPress) {
    if (voiceMode() && voice_ % 2 == 1 && voiceViews_[voice_]->page() == 0) {
        voiceViews_[voice_ - 1]->goToPage(voiceViews_[voice_ - 1]->pages() - 1);
        showVoice(voice_ - 1);
        return;
    }
    base_type::eventUp(longPress);
}

void PluginEditor::eventDown(bool longPress) {
    auto& vv = voiceViews_[voice_];
    if (voiceMode() && voice_ % 2 == 0 && vv->page() + 1 == vv->pages()) {
        voiceViews_[voice_ + 1]->goToPage(0);
        showVoice(voice_ + 1);
        return;
    }
    base_type::eventDown(longPress);
}

// TRK-/TRK+ step through the tracks (to each one's L voice), then the global view.
// BaseViewEditor's own left/right would step through every view, including the browser.
void PluginEditor::onLeftButton(bool v) {
    if (fileMode() || saveMode()) {
        views_[view_]->onLeftButton(v);
        return;
    }
    leftBtn_.onButton(v);
    if (v) return;
    if (globalMode()) showVoice((TRACKS - 1) * 2);
    else if (voice_ >= 2) showVoice((voice_ / 2 - 1) * 2);
}

void PluginEditor::onRightButton(bool v) {
    if (fileMode() || saveMode()) {
        views_[view_]->onRightButton(v);
        return;
    }
    rightBtn_.onButton(v);
    if (v || globalMode()) return;
    if (voice_ / 2 + 1 < TRACKS) showVoice((voice_ / 2 + 1) * 2);
    else setView(V_GLOBAL);
}

static float normValue(RangedAudioParameter& p) {
    return p.convertFrom0to1(p.getValue());
}

// waveform area: right of the param bars, above the button grid
static constexpr int waveX = 910;
static constexpr int waveW = int(sfct::PEAK_BINS);  // one bin per pixel
static constexpr int waveY = 45;
static constexpr int laneH = 155;
static constexpr int laneGap = 10;

void PluginEditor::drawView(Graphics& g) {
    base_type::drawView(g);
    if (!voiceMode() && !globalMode()) return;

    float inL, inR, outL, outR;
    processor_.getRMS(inL, inR, outL, outR);
    inVu_.level(inL, inR);
    outVu_.level(outL, outR);

    auto font = Font(FontOptions(Font::getDefaultMonospacedFontName(), 20, Font::plain));
    g.setFont(font);
    unsigned track = voice_ / 2;
    bool shared = processor_.mode() == sfct::SHARED;
    if (globalMode()) {
        g.setColour(Colours::white);
        g.drawText("Global", waveX, 8, 200, 30, Justification::left);
    } else {
        g.setColour(trackClrs[track]);
        g.drawText("Track " + String(track + 1) + (voice_ % 2 ? " R " : " L ") +
                       String(voiceViews_[voice_]->page() + 1) + "/" + String(voiceViews_[voice_]->pages()),
                   waveX, 8, 200, 30, Justification::left);
    }
    g.setColour(Colours::white);
    g.drawText(String::formatted("DSP %5.1f%%  peak %5.1f%%", processor_.loadAverage() * 100.0f,
                                 processor_.loadPeak() * 100.0f),
               waveX + 200, 8, waveW - 200, 30, Justification::right);

    float len = processor_.viewSeconds();
    auto xOf = [len](float t) { return float(waveX) + t / len * float(waveW); };

    // lanes: the current track's L and R buffers, with every enabled voice that uses them
    for (unsigned l = 0; l < sfct::LANES; l++) {
        unsigned b = processor_.bufferOf(track * 2 + l);
        int top = waveY + int(l) * (laneH + laneGap);
        g.setColour(Colour(24, 24, 24));
        g.fillRect(waveX, top, waveW, laneH);

        // loop regions, the current voice's brighter
        for (unsigned v = 0; v < VOICES; v++) {
            auto& vp = processor_.getVoice(v);
            if (processor_.bufferOf(v) != b || vp.on.getValue() < 0.5f) continue;
            float s = normValue(vp.start), e = normValue(vp.end);
            float x0 = xOf(std::min(std::min(s, e), len)), x1 = xOf(std::min(std::max(s, e), len));
            g.setColour(trackClrs[v / 2].withAlpha(v == voice_ ? 0.25f : 0.09f));
            g.fillRect(x0, float(top), x1 - x0, float(laneH));
        }

        float mid = float(top) + laneH * 0.5f;
        g.setColour(Colours::lightgrey);
        for (unsigned bin = 0; bin < sfct::PEAK_BINS; bin++) {
            float h = std::min(processor_.peak(l, bin), 1.0f) * laneH * 0.5f;
            if (h >= 0.5f) g.drawVerticalLine(waveX + int(bin), mid - h, mid + h);
        }

        // both subheads, each as bright as its gain: in a crossfade one fades out as the other fades in
        for (unsigned v = 0; v < VOICES; v++) {
            auto& vp = processor_.getVoice(v);
            if (processor_.bufferOf(v) != b || vp.on.getValue() < 0.5f) continue;
            if (vp.play.getValue() < 0.5f && vp.rec.getValue() < 0.5f) continue;
            float w = processor_.isRecording(v) ? 3.0f : 1.5f;
            for (int i = 0; i < 2; i++) {
                float gain = processor_.headGain(v, i);
                float pos = processor_.headPosition(v, i);
                if (gain < 0.01f || pos < 0.0f || pos > len) continue;
                g.setColour(trackClrs[v / 2].withAlpha(gain));
                g.fillRect(xOf(pos) - w * 0.5f, float(top), w, float(laneH));
            }
        }

        String fn = processor_.getBufferFile(b);
        g.setColour(l == voice_ % 2 ? Colours::white : Colours::grey);
        String side = String(shared ? "" : "T" + String(track + 1) + " ") + (l ? "R " : "L ");
        g.drawText(side + (fn.isEmpty() ? String() : File(fn).getFileName()), waveX + 6, top + 2, waveW - 12, 24,
                   Justification::left);
    }

    int bottom = waveY + 2 * laneH + laneGap;
    g.setColour(Colours::grey);
    g.drawText("0 s", waveX + 6, bottom - 24, 100, 22, Justification::left);
    g.drawText(String::formatted("%.2f s", len), waveX + waveW - 106, bottom - 24, 100, 22, Justification::right);
    g.setColour(Colours::yellow);
    g.drawText(processor_.saveStatus(), waveX + 110, bottom - 24, waveW - 220, 22, Justification::centred);
}

void PluginEditor::resized() {
    base_type::resized();
    const unsigned h = 130;
    const unsigned sp = 10;
    const unsigned vuW = 45;
    unsigned x = 1500;
    unsigned y = waveY;
    inVu_.setBounds(x, y, vuW, h);
    x += vuW + sp;
    outVu_.setBounds(x, y, vuW, h);
}
