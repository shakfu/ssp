#include "PluginMiniEditor.h"

#include "PluginProcessor.h"
#include "ssp/controls/MiniControl.h"
#include "ssp/controls/ParamButton.h"

using pcontrol_type = ssp::MiniControl;
using bcontrol_type = ssp::ParamButton;

PluginMiniEditor::PluginMiniEditor(PluginProcessor& p) : base_type(&p), processor_(p) {
    static constexpr unsigned VOICES = PluginProcessor::VOICES;
    static const juce::Colour clrs[sfct::TRACKS] = { juce::Colour(230, 90, 80), juce::Colour(80, 170, 230),
                                                     juce::Colour(120, 200, 110), juce::Colour(220, 180, 60) };
    static constexpr unsigned fh = 12 * COMPACT_UI_SCALE;

    for (unsigned v = 0; v < VOICES; v++) {
        auto& vp = processor_.getVoice(v);
        auto clr = clrs[v / 2];
        addButtonPage(std::make_shared<bcontrol_type>(vp.play, fh, clr),
                      std::make_shared<bcontrol_type>(vp.rec, fh, Colours::red),
                      std::make_shared<bcontrol_type>(vp.loop, fh, clr),
                      std::make_shared<bcontrol_type>(vp.cut, fh, clr, Colours::black, true),
                      std::make_shared<bcontrol_type>(vp.on, fh, clr),
                      std::make_shared<bcontrol_type>(vp.link, fh, Colours::white), nullptr, nullptr);
        addParamPage(std::make_shared<pcontrol_type>(vp.rate, 0.05f, 0.001f),
                     std::make_shared<pcontrol_type>(vp.start, 1.0f, 0.01f),
                     std::make_shared<pcontrol_type>(vp.end, 1.0f, 0.01f),
                     std::make_shared<pcontrol_type>(vp.level, 0.1f, 0.01f), clr);
        addParamPage(std::make_shared<pcontrol_type>(vp.rec_level, 0.1f, 0.01f),
                     std::make_shared<pcontrol_type>(vp.pre_level, 0.1f, 0.01f),
                     std::make_shared<pcontrol_type>(vp.in_gain, 0.1f, 0.01f),
                     std::make_shared<pcontrol_type>(vp.pan, 0.1f, 0.01f), clr);
        auto c = [](RangedAudioParameter& p, float coarse, float fine) {
            return std::make_shared<pcontrol_type>(p, coarse, fine);
        };
        addParamPage(c(vp.fade, 0.01f, 0.001f), c(vp.slew, 0.1f, 0.01f), c(vp.rec_shape, 1.0f, 1.0f),
                     c(vp.pre_shape, 1.0f, 1.0f), clr);
        addParamPage(c(vp.rec_delay, 0.01f, 0.001f), c(vp.pre_window, 0.05f, 0.005f), c(vp.rec_offset, 0.1f, 0.01f),
                     nullptr, clr);
        addParamPage(c(vp.lpf, 500.0f, 10.0f), c(vp.post_rq, 0.1f, 0.01f), c(vp.lp_mix, 0.1f, 0.01f),
                     c(vp.post_dry, 0.1f, 0.01f), clr);
        addParamPage(c(vp.post_hp, 0.1f, 0.01f), c(vp.post_bp, 0.1f, 0.01f), c(vp.post_br, 0.1f, 0.01f), nullptr, clr);
        addParamPage(c(vp.pre_fc, 500.0f, 10.0f), c(vp.pre_rq, 0.1f, 0.01f), c(vp.pre_fc_mod, 0.1f, 0.01f),
                     c(vp.pre_dry, 0.1f, 0.01f), clr);
        addParamPage(c(vp.pre_lp, 0.1f, 0.01f), c(vp.pre_hp, 0.1f, 0.01f), c(vp.pre_bp, 0.1f, 0.01f),
                     c(vp.pre_br, 0.1f, 0.01f), clr);
    }
    addParamPage(std::make_shared<pcontrol_type>(*processor_.getParameter(ID::mode), 1.0f, 1.0f), nullptr, nullptr,
                 nullptr, Colours::white);
    auto input = [&](unsigned t) { return std::make_shared<pcontrol_type>(processor_.getTrack(t).input, 1.0f, 1.0f); };
    addParamPage(input(0), input(1), input(2), input(3), Colours::white);
    for (unsigned t = 0; t < sfct::TRACKS; t += 2) {
        auto& a = processor_.getTrack(t);
        auto& b = processor_.getTrack(t + 1);
        addParamPage(std::make_shared<pcontrol_type>(a.fb_src, 1.0f, 1.0f),
                     std::make_shared<pcontrol_type>(a.fb_amt, 0.1f, 0.01f),
                     std::make_shared<pcontrol_type>(b.fb_src, 1.0f, 1.0f),
                     std::make_shared<pcontrol_type>(b.fb_amt, 0.1f, 0.01f), Colours::white);
    }
    auto sync = [&](unsigned t) { return std::make_shared<pcontrol_type>(processor_.getTrack(t).sync, 1.0f, 1.0f); };
    addParamPage(sync(0), sync(1), sync(2), sync(3), Colours::white);
    for (unsigned t = 0; t < sfct::TRACKS; t += 2) {
        auto& a = processor_.getTrack(t);
        auto& b = processor_.getTrack(t + 1);
        addParamPage(std::make_shared<pcontrol_type>(a.phase_q, 0.25f, 0.01f),
                     std::make_shared<pcontrol_type>(a.phase_off, 0.25f, 0.01f),
                     std::make_shared<pcontrol_type>(b.phase_q, 0.25f, 0.01f),
                     std::make_shared<pcontrol_type>(b.phase_off, 0.25f, 0.01f), Colours::white);
    }
}
