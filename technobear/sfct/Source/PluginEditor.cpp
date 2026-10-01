#include "PluginEditor.h"

#include "FadeZones.h"

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

static const char* voicePageNames[] = { "play", "rec", "fade", "fade 2", "filter", "filter 2", "in filter", "in filter 2" };
static const char* globalPageNames[] = { "mode", "input", "feedback 1-2", "feedback 3-4", "sync", "phase 1-2", "phase 3-4" };
static constexpr unsigned SYNC_PAGE = 4;  // globalPageNames[SYNC_PAGE] == "sync"

// Param pages with an index into a name table, and a way to jump to a page.
class PagedView : public ssp::BarParamEditor {
public:
    explicit PagedView(PluginProcessor& p) : ssp::BarParamEditor(&p, false) {}
    unsigned page() const { return paramPage_; }
    unsigned pages() const { return unsigned(controlPages_.size()); }
    void goToPage(unsigned p) {
        while (paramPage_ < p && paramPage_ + 1 < pages()) chgParamPage(1, false);
        while (paramPage_ > p) chgParamPage(-1, false);
    }

protected:
    using ctl = std::shared_ptr<ssp::BaseParamControl>;
    ctl c(RangedAudioParameter& p, float coarse, float fine, Colour clr) {
        return std::make_shared<pcontrol_type>(p, coarse, fine, clr);
    }
};

class VoiceView : public PagedView {
public:
    VoiceView(PluginProcessor& p, unsigned v) : PagedView(p) {
        auto& vp = p.getVoice(v);
        auto k = trackClrs[v / 2];
        addParamPage(c(vp.rate, 0.05f, 0.001f, k), c(vp.start, 1.0f, 0.01f, k), c(vp.end, 1.0f, 0.01f, k),
                     c(vp.level, 0.1f, 0.01f, k));
        addParamPage(c(vp.rec_level, 0.1f, 0.01f, k), c(vp.pre_level, 0.1f, 0.01f, k), c(vp.in_gain, 0.1f, 0.01f, k),
                     c(vp.pan, 0.1f, 0.01f, k));
        addParamPage(c(vp.fade, 0.01f, 0.001f, k), c(vp.slew, 0.1f, 0.01f, k), c(vp.rec_shape, 1.0f, 1.0f, k),
                     c(vp.pre_shape, 1.0f, 1.0f, k));
        addParamPage(c(vp.rec_delay, 0.01f, 0.001f, k), c(vp.pre_window, 0.05f, 0.005f, k),
                     c(vp.rec_offset, 0.1f, 0.01f, k), nullptr);
        addParamPage(c(vp.lpf, 500.0f, 10.0f, k), c(vp.post_rq, 0.1f, 0.01f, k), c(vp.lp_mix, 0.1f, 0.01f, k),
                     c(vp.post_dry, 0.1f, 0.01f, k));
        addParamPage(c(vp.post_hp, 0.1f, 0.01f, k), c(vp.post_bp, 0.1f, 0.01f, k), c(vp.post_br, 0.1f, 0.01f, k),
                     nullptr);
        addParamPage(c(vp.pre_fc, 500.0f, 10.0f, k), c(vp.pre_rq, 0.1f, 0.01f, k), c(vp.pre_fc_mod, 0.1f, 0.01f, k),
                     c(vp.pre_dry, 0.1f, 0.01f, k));
        addParamPage(c(vp.pre_lp, 0.1f, 0.01f, k), c(vp.pre_hp, 0.1f, 0.01f, k), c(vp.pre_bp, 0.1f, 0.01f, k),
                     c(vp.pre_br, 0.1f, 0.01f, k));
        jassert(pages() == std::size(voicePageNames));
        addButtonPage(std::make_shared<bcontrol_type>(vp.play, 24, k),
                      std::make_shared<bcontrol_type>(vp.rec, 24, Colours::red),
                      std::make_shared<bcontrol_type>(vp.loop, 24, k),
                      std::make_shared<bcontrol_type>(vp.cut, 24, k, Colours::black, true),
                      nullptr,  // Load
                      std::make_shared<bcontrol_type>(vp.on, 24, k),
                      std::make_shared<bcontrol_type>(vp.link, 24, Colours::white), nullptr);
    }
};

class GlobalView : public PagedView {
public:
    explicit GlobalView(PluginProcessor& p) : PagedView(p) {
        addParamPage(c(*p.getParameter(ID::mode), 1.0f, 1.0f, Colours::white), nullptr, nullptr, nullptr);
        ctl input[TRACKS];
        for (unsigned t = 0; t < TRACKS; t++) input[t] = c(p.getTrack(t).input, 1.0f, 1.0f, trackClrs[t]);
        addParamPage(input[0], input[1], input[2], input[3]);
        // feedback: two tracks per page, each a source and an amount
        for (unsigned t = 0; t < TRACKS; t += 2) {
            auto &a = p.getTrack(t), &b = p.getTrack(t + 1);
            addParamPage(c(a.fb_src, 1.0f, 1.0f, trackClrs[t]), c(a.fb_amt, 0.1f, 0.01f, trackClrs[t]),
                         c(b.fb_src, 1.0f, 1.0f, trackClrs[t + 1]), c(b.fb_amt, 0.1f, 0.01f, trackClrs[t + 1]));
        }
        ctl sync[TRACKS];
        for (unsigned t = 0; t < TRACKS; t++) sync[t] = c(p.getTrack(t).sync, 1.0f, 1.0f, trackClrs[t]);
        addParamPage(sync[0], sync[1], sync[2], sync[3]);
        for (unsigned t = 0; t < TRACKS; t += 2) {
            auto &a = p.getTrack(t), &b = p.getTrack(t + 1);
            addParamPage(c(a.phase_q, 0.25f, 0.01f, trackClrs[t]), c(a.phase_off, 0.25f, 0.01f, trackClrs[t]),
                         c(b.phase_q, 0.25f, 0.01f, trackClrs[t + 1]), c(b.phase_off, 0.25f, 0.01f, trackClrs[t + 1]));
        }
        jassert(pages() == std::size(globalPageNames));
        addButtonPage(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    }
};

PluginEditor::PluginEditor(PluginProcessor& p)
    : base_type(&p),
      loadBtn_("Load", [&](bool b) { onLoadButton(b); }, 24, Colours::cyan),
      saveBtn_("Save", [&](bool b) { onSaveButton(b); }, 24, Colours::yellow),
      cancelBtn_("Cancel", [&](bool b) { onCancelButton(b); }, 24, Colours::white),
      processor_(p) {
    for (auto& fc : fadeCurves_) fc.init(true);
    leftBtn_.label("TRK-");
    rightBtn_.label("TRK+");
    rightShiftBtn_.label("CLR");  // BaseEditor creates RS but leaves it off screen
    addAndMakeVisible(rightShiftBtn_);

    for (unsigned v = 0; v < VOICES; v++) {
        voiceViews_.push_back(std::make_shared<VoiceView>(p, v));
        addView(voiceViews_.back());
    }
    globalView_ = std::make_shared<GlobalView>(p);
    addView(globalView_);

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
    for (auto* c : std::initializer_list<Component*>{ &leftBtn_, &rightBtn_, &upBtn_, &downBtn_, &rightShiftBtn_,
                                                      &inVu_, &outVu_ }) {
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

// Long-pressing rec records one pass. A long press does not toggle a button.
void PluginEditor::eventButton(unsigned btn, bool longPress) {
    base_type::eventButton(btn, longPress);
    if (longPress && !consumeHeld(btn)) holdAction(btn);
}

// Holds act once the framework's long-press threshold is reached, while the button is still down;
// the release that follows reports a long press, which consumeHeld() then swallows. Clear (RS) is
// left to the release, because RS also opens the system panel with LS: acting at the threshold would
// clear whenever RS is held a moment before LS. On release the framework consumes that combo first.
void PluginEditor::eventButtonHeld(unsigned btn) {
    base_type::eventButtonHeld(btn);
    if (btn != SSP_Shift_R && holdAction(btn)) heldActed_[btn] = true;
}

bool PluginEditor::consumeHeld(unsigned btn) {
    bool acted = heldActed_[btn];
    heldActed_[btn] = false;
    return acted;
}

bool PluginEditor::holdAction(unsigned btn) {
    bool main = voiceMode() || globalMode();
    switch (btn) {
        case SSP_Left:  // TRK-: track 1
            if (!main) return false;
            showVoice(0);
            return true;
        case SSP_Right:  // TRK+: the global view
            if (!main) return false;
            setView(V_GLOBAL);
            return true;
        case SSP_Up:
        case SSP_Down: return switchSide();
        case SSP_Shift_R:
            if (!main) return false;
            if (voiceMode()) processor_.clearLoop(voice_);
            else processor_.clearAll();
            return true;
        case B_REC:  // record once
            if (!voiceMode()) return false;
            processor_.recOnce(voice_);
            return true;
        default: return false;
    }
}

// Holding EN-/EN+ switches to the other side of the track, at the same page.
bool PluginEditor::switchSide() {
    if (!voiceMode()) return false;
    unsigned other = sfct::partnerOf(voice_);
    voiceViews_[other]->goToPage(voiceViews_[voice_]->page());
    showVoice(other);
    return true;
}

// EN-/EN+ page through a stereo pair as one sequence: the left voice's pages, then the right's
void PluginEditor::eventUp(bool longPress) {
    if (longPress && (consumeHeld(SSP_Up) || switchSide())) return;
    if (voiceMode() && voice_ % 2 == 1 && voiceViews_[voice_]->page() == 0) {
        voiceViews_[voice_ - 1]->goToPage(voiceViews_[voice_ - 1]->pages() - 1);
        showVoice(voice_ - 1);
        return;
    }
    base_type::eventUp(longPress);
}

void PluginEditor::eventDown(bool longPress) {
    if (longPress && (consumeHeld(SSP_Down) || switchSide())) return;
    auto& vv = voiceViews_[voice_];
    if (voiceMode() && voice_ % 2 == 0 && vv->page() + 1 == vv->pages()) {
        voiceViews_[voice_ + 1]->goToPage(0);
        showVoice(voice_ + 1);
        return;
    }
    base_type::eventDown(longPress);
}

// Holding RS clears: the current voice's loop in a track, everything in the Global view.
// A short press does nothing, as clearing is destructive.
void PluginEditor::eventRightShift(bool longPress) {
    if (voiceMode() || globalMode()) {
        if (longPress) holdAction(SSP_Shift_R);
        return;
    }
    base_type::eventRightShift(longPress);
}

// TRK-/TRK+ step through the tracks (to each one's L voice), then the global view. Held, TRK- returns to
// track 1 and TRK+ jumps to the global view. The press only lights the button; eventLeft/eventRight act on
// release, when the framework knows whether it was a long press. BaseViewEditor's own left/right would
// step through every view, including the browser.
void PluginEditor::onLeftButton(bool v) {
    if (fileMode() || saveMode()) {
        views_[view_]->onLeftButton(v);
        return;
    }
    leftBtn_.onButton(v);
}

void PluginEditor::onRightButton(bool v) {
    if (fileMode() || saveMode()) {
        views_[view_]->onRightButton(v);
        return;
    }
    rightBtn_.onButton(v);
}

void PluginEditor::eventLeft(bool longPress) {
    if (fileMode() || saveMode()) {
        base_type::eventLeft(longPress);
        return;
    }
    if (longPress) {
        if (!consumeHeld(SSP_Left)) holdAction(SSP_Left);
    } else if (globalMode()) showVoice((TRACKS - 1) * 2);
    else if (voice_ >= 2) showVoice((voice_ / 2 - 1) * 2);
}

void PluginEditor::eventRight(bool longPress) {
    if (fileMode() || saveMode()) {
        base_type::eventRight(longPress);
        return;
    }
    if (longPress) {
        if (!consumeHeld(SSP_Right)) holdAction(SSP_Right);
    } else if (globalMode()) {
        return;
    } else if (voice_ / 2 + 1 < TRACKS) {
        showVoice((voice_ / 2 + 1) * 2);
    } else {
        setView(V_GLOBAL);
    }
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
    static constexpr int headW = 330;
    if (globalMode()) {
        unsigned pg = globalView_->page();
        float bpm = processor_.bpm();  // on the sync page only: the header has room for one extra field
        g.setColour(Colours::white);
        g.drawText("Global " + String(pg + 1) + "/" + String(globalView_->pages()) + " " + globalPageNames[pg] +
                       (pg != SYNC_PAGE ? String() : bpm > 0.0f ? String::formatted(" %.1f bpm", bpm) : String(" no clock")),
                   waveX, 8, headW, 30, Justification::left);
    } else {
        unsigned pg = voiceViews_[voice_]->page();
        g.setColour(trackClrs[track]);
        g.drawText("Track " + String(track + 1) + (voice_ % 2 ? " R " : " L ") + String(pg + 1) + "/" +
                       String(voiceViews_[voice_]->pages()) + " " + voicePageNames[pg],
                   waveX, 8, headW, 30, Justification::left);
    }
    g.setColour(Colours::white);
    g.drawText(String::formatted("DSP %4.1f%% pk %4.1f%%", processor_.loadAverage() * 100.0f,
                                 processor_.loadPeak() * 100.0f),
               waveX + headW, 8, waveW - headW, 30, Justification::right);

    float len = processor_.viewSeconds();
    auto xOf = [len](float t) { return float(waveX) + t / len * float(waveW); };

    // lanes: the current track's L and R buffers, with every enabled voice that uses them
    for (unsigned l = 0; l < sfct::LANES; l++) {
        unsigned b = processor_.bufferOf(track * 2 + l);
        int top = waveY + int(l) * (laneH + laneGap);
        Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(waveX, top, waveW, laneH);
        g.setColour(Colour(24, 24, 24));
        g.fillRect(waveX, top, waveW, laneH);

        // loop regions as played (after CV), the current voice's brighter
        for (unsigned v = 0; v < VOICES; v++) {
            auto& vp = processor_.getVoice(v);
            if (processor_.bufferOf(v) != b || vp.on.getValue() < 0.5f) continue;
            float s = processor_.playedStart(v), e = processor_.playedEnd(v);
            float x0 = xOf(std::min(std::min(s, e), len)), x1 = xOf(std::min(std::max(s, e), len));
            bool cur = v == voice_;
            g.setColour(trackClrs[v / 2].withAlpha(cur ? 0.25f : 0.09f));
            g.fillRect(x0, float(top), x1 - x0, float(laneH));
            drawFades(g, v, top, len);
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

    g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 14, Font::plain)));
    g.setColour(Colours::grey);
    g.drawText("fades: solid gain, dashed rec, grey kept", waveX + 6, waveY + laneH + laneGap + 24, waveW - 12, 18,
               Justification::right);
    g.setFont(font);

    int bottom = waveY + 2 * laneH + laneGap;
    g.setColour(Colours::grey);
    g.drawText("0 s", waveX + 6, bottom - 24, 100, 22, Justification::left);
    g.drawText(String::formatted("%.2f s", len), waveX + waveW - 106, bottom - 24, 100, 22, Justification::right);
    g.setColour(Colours::yellow);
    g.drawText(processor_.saveStatus(), waveX + 110, bottom - 24, waveW - 220, 22, Justification::centred);
}

// Across each crossfade, from 0 at the lane's bottom to 0.9 of its height: the voice's playback
// gain; for the current voice while recording, also the rec level (dashed) and the kept level
// (grey) that the fade curves apply.
void PluginEditor::drawFades(Graphics& g, unsigned v, int top, float len) {
    auto& vp = processor_.getVoice(v);
    float fadeTime = normValue(vp.fade);
    sfct::FadeZone zones[2];
    unsigned nz = sfct::fadeZones(processor_.playedStart(v), processor_.playedEnd(v), processor_.playedRate(v),
                                  vp.loop.getValue() > 0.5f, fadeTime, zones);
    bool cur = v == voice_;
    bool rec = cur && processor_.isRecording(v);
    float recLvl = normValue(vp.rec_level), preLvl = normValue(vp.pre_level);
    // this voice's curve settings; FadeCurves rebuilds its tables on each set, so only on change
    auto& fc = fadeCurves_[v];
    FadeSettings want{ int(normValue(vp.rec_shape)), int(normValue(vp.pre_shape)), normValue(vp.rec_delay),
                       normValue(vp.pre_window) };
    if (rec && !(want == fadeSettings_[v])) {
        fc.setRecShape(softcut::FadeCurves::Shape(want.recShape));
        fc.setPreShape(softcut::FadeCurves::Shape(want.preShape));
        fc.setRecDelayRatio(want.recDelay);
        fc.setPreWindowRatio(want.preWindow);
        fadeSettings_[v] = want;
    }
    auto x = [len](float t) { return float(waveX) + t / len * float(waveW); };
    auto y = [top](float level) { return float(top + laneH) - level * laneH * 0.9f; };
    auto colour = trackClrs[v / 2];

    for (unsigned z = 0; z < nz; z++) {
        Path gain, recPath, prePath;
        static constexpr int N = 32;
        for (int k = 0; k <= N; k++) {
            float progress = float(k) / N;
            float px = x(zones[z].time(progress, fadeTime));
            float fade = zones[z].fade(progress);
            auto add = [&](Path& p, float level) {
                if (k == 0) p.startNewSubPath(px, y(level));
                else p.lineTo(px, y(level));
            };
            add(gain, sfct::fadeGain(fade));
            if (rec) {
                add(recPath, recLvl * fc.getRecFadeValue(fade));
                add(prePath, preLvl + (1.0f - preLvl) * fc.getPreFadeValue(fade));
            }
        }
        g.setColour(colour.withAlpha(cur ? 1.0f : 0.43f));
        g.strokePath(gain, PathStrokeType(cur ? 2.0f : 1.0f));
        if (rec) {
            Path dashed;
            static const float dashes[] = { 6.0f, 4.0f };
            PathStrokeType(1.5f).createDashedStroke(dashed, recPath, dashes, 2);
            g.setColour(colour);
            g.fillPath(dashed);
            g.setColour(Colours::grey);
            g.strokePath(prePath, PathStrokeType(1.5f));
        }
    }
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
