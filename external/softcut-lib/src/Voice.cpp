//
// Created by ezra on 11/3/18.
//

#include <functional>

#include "softcut/Voice.h"
#include "softcut/Resampler.h"

using namespace softcut;

Voice::Voice(bool fix) :
rateRamp(48000, 0.1),
preRamp(48000, 0.1),
recRamp(48000, 0.1)
{
    fixQuirks = fix;
    svfPreFcBase = 16000;
    reset();
}

void Voice::reset() {
    fadeCurves.init(fixQuirks);

    svfPre.reset();
    svfPre.setLpMix(1.0);
    svfPre.setHpMix(0.0);
    svfPre.setBpMix(0.0);
    svfPre.setBrMix(0.0);
    svfPre.setRq(4.0);
    svfPre.setFc(svfPreFcBase);
    svfPreFcMod = 1.0;
    svfPreDryLevel = 0.0;

    svfPost.reset();
    svfPost.setLpMix(0.0);
    svfPost.setHpMix(0.0);
    svfPost.setBpMix(0.0);
    svfPost.setBrMix(0.0);
    svfPost.setRq(4.0);
    svfPost.setFc(12000);
    svfPostDryLevel = 1.0;

    rateRamp.reset(1.0);
    recRamp.reset(0.0);
    preRamp.reset(0.0);

    setFadeTime(0.01);
    setRecPreSlewTime(0.001);
    setRateSlewTime(0.001);

    sch.setRecOffsetSamples(-8);

    // Restore the phase state too, so a reset voice reports where it now is
    // rather than where it was, and so the defaults the host advertises for
    // these two are the ones reset() actually establishes. Assigned rather than
    // set through setPhaseOffset(), which scales by sampleRate -- not yet set
    // when the constructor calls reset(), and a garbage sampleRate of inf or
    // NaN would survive the multiply by zero.
    phaseQuant = 0;
    phaseOffset = 0;
    rawPhase.store(0, std::memory_order_relaxed);
    quantPhase.store(0, std::memory_order_relaxed);

    recFlag = false;
    playFlag = false;

    sch.init(&fadeCurves);
    // sch.init() sets 0.1 s, overriding the setFadeTime(0.01) above
    if (fixQuirks) {
        setFadeTime(0.01);
    }
    publishFlags();
    publishHeads();
}

void Voice:: processBlockMono(const float *in, float *out, int numFrames) {
    std::function<void(sample_t, sample_t*)> sampleFunc;
    if(playFlag) {
        if(recFlag) {
            sampleFunc = [this](float in, float* out) {
                this->sch.processSample(in, out);
            };
        } else {
            sampleFunc = [this](float in, float* out) {
                this->sch.processSampleNoWrite(in, out);
            };
        }
    } else {
        if(recFlag) {
            sampleFunc = [this](float in, float* out) {
                this->sch.processSampleNoRead(in, out);
            };
        } else {
            sampleFunc = [](float in, float* out) {
                (void)in;
                // makes sure the output bus is zeroed
                *out = 0.f;
            };
        }
    }

    float x, y;
    for(int i=0; i<numFrames; ++i) {
        x = svfPre.getNextSample(in[i]) + in[i]*svfPreDryLevel;
        sch.setRate(rateRamp.update());
        sch.setPre(preRamp.update());
        sch.setRec(recRamp.update());
        sampleFunc(x, &y);
	    out[i] = svfPost.getNextSample(y) + y*svfPostDryLevel;
        updateQuantPhase();
    }

    
    rawPhase.store(sch.getActivePhase(), std::memory_order_relaxed);

    if(recFlag) {
        if (sch.getRecOnceDone()) {
            // record once is finished, turn off recording flag
            // and reset the recording subheads
	    recFlag = false;
            sch.setRecOnceFlag(false);
        }
    }
    publishFlags();
    publishHeads();
}

void Voice::setSampleRate(float hz) {
    sampleRate = hz;
    rateRamp.setSampleRate(hz);
    preRamp.setSampleRate(hz);
    recRamp.setSampleRate(hz);
    sch.setSampleRate(hz);
    svfPre.setSampleRate(hz);
    svfPost.setSampleRate(hz);
}

void Voice::setRate(float rate) {    
    rateRamp.setTarget(rate);
    updatePreSvfFc();
}

void Voice::setLoopStart(float sec) {
    sch.setLoopStartSeconds(sec);
}

void Voice::setLoopEnd(float sec) {
    sch.setLoopEndSeconds(sec);
}

void Voice::setFadeTime(float sec) {
    sch.setFadeTime(sec);
}

void Voice::cutToPos(float sec) {
    sch.cutToPos(sec);
}

void Voice::setRecLevel(float amp) {
    recRamp.setTarget(amp);
}

void Voice::setPreLevel(float amp) {
    preRamp.setTarget(amp);
}

void Voice::setRecFlag(bool val) {
    if (recFlag) {
	if (!(val || playFlag)) {
	    sch.stop();
	}
    } else {
	if (val && !playFlag) {
	    sch.run();
	}
    }
    recFlag = val;
    if (!val) {
	// turn off rec once if active
        if (sch.getRecOnceActive()) {
            sch.setRecOnceFlag(false);
	}
    }
    publishFlags();
}

void Voice::setPlayFlag(bool val) {
    if (playFlag) {
	if (!(val || recFlag)) {
	    sch.stop();
	}
    } else {
	if (val && !recFlag) {
	    sch.run();
	}
    }
    playFlag = val;
}

void Voice::setLoopFlag(bool val) {
    sch.setLoopFlag(val);
}

// input filter
void Voice::setPreFilterFc(float x) {
    svfPreFcBase = x;
    updatePreSvfFc();
}

void Voice::setPreFilterRq(float x) {
    svfPre.setRq(x);
}

void Voice::setPreFilterLp(float x) {
    svfPre.setLpMix(x);
}

void Voice::setPreFilterHp(float x) {
    svfPre.setHpMix(x);
}

void Voice::setPreFilterBp(float x) {
    svfPre.setBpMix(x);
}

void Voice::setPreFilterBr(float x) {
    svfPre.setBrMix(x);
}

void Voice::setPreFilterDry(float x) {
    svfPreDryLevel = x;
}

void Voice::setPreFilterFcMod(float x) {
    svfPreFcMod = x;
}

void Voice::updatePreSvfFc() {
    float fcMod = std::min(svfPreFcBase, svfPreFcBase * std::fabs(static_cast<float>(sch.getRate())));
    fcMod = svfPreFcBase + svfPreFcMod * (fcMod - svfPreFcBase);
    svfPre.setFc(fcMod);
}

// output filter
void Voice::setPostFilterFc(float x) {
    svfPost.setFc(x);
}

void Voice::setPostFilterRq(float x) {
    svfPost.setRq(x);
}

void Voice::setPostFilterLp(float x) {
    svfPost.setLpMix(x);
}

void Voice::setPostFilterHp(float x) {
    svfPost.setHpMix(x);
}

void Voice::setPostFilterBp(float x) {
    svfPost.setBpMix(x);
}

void Voice::setPostFilterBr(float x) {
    svfPost.setBrMix(x);
}

void Voice::setPostFilterDry(float x) {
    svfPostDryLevel = x;
}

void Voice::setRecOnceFlag(bool val) {
    sch.setRecOnceFlag(val);
    if (val) {
        setRecFlag(true);
    }
    publishFlags();
}

void Voice::setBuffer(float *b, unsigned int nf) {
    buf = b;
    bufFrames = nf;
    sch.setBuffer(buf, bufFrames);
}

void Voice::setRecOffset(float d) {
    sch.setRecOffsetSamples(static_cast<int>(d * sampleRate));
}

void Voice::setRecPreSlewTime(float d) {
    recRamp.setTime(d);
    preRamp.setTime(d);
}

void Voice::setRateSlewTime(float d) {
    rateRamp.setTime(d);
}

void Voice::setPhaseQuant(float x) {
    phaseQuant = x;
}

void Voice::setPhaseOffset(float x) {
    phaseOffset = x * sampleRate;
}

phase_t Voice::getQuantPhase() {
    return quantPhase.load(std::memory_order_relaxed);
}

void Voice::updateQuantPhase() {
    if (phaseQuant == 0) {
        quantPhase.store(sch.getActivePhase() / sampleRate, std::memory_order_relaxed);
    } else {
	const phase_t tmp = (sch.getActivePhase() + phaseOffset) / (sampleRate *phaseQuant);
        quantPhase.store(std::floor(tmp) * phaseQuant, std::memory_order_relaxed);
    }
}

bool Voice::getPlayFlag() {
    return playFlag;
}

bool Voice::getRecFlag() {
    return recFlag;
}

bool Voice::getSavedRecFlag() {
    return savedRecFlag.load(std::memory_order_relaxed);
}

bool Voice::getSavedRecOnceFlag() {
    return savedRecOnceFlag.load(std::memory_order_relaxed);
}

float Voice::getSavedHeadPosition(int i) {
    return static_cast<float>(savedHeadPhase[i].load(std::memory_order_relaxed) / sampleRate);
}

float Voice::getSavedHeadFade(int i) {
    return savedHeadFade[i].load(std::memory_order_relaxed);
}

int Voice::getSavedActiveHead() {
    return savedActiveHead.load(std::memory_order_relaxed);
}

void Voice::publishHeads() {
    for (int i = 0; i < 2; ++i) {
        savedHeadPhase[i].store(sch.getHeadPhase(i), std::memory_order_relaxed);
        savedHeadFade[i].store(sch.getHeadFade(i), std::memory_order_relaxed);
    }
    savedActiveHead.store(sch.getActiveHead(), std::memory_order_relaxed);
}

void Voice::setRecFadeShape(FadeCurves::Shape shape) {
    fadeCurves.setRecShape(shape);
}

void Voice::setPreFadeShape(FadeCurves::Shape shape) {
    fadeCurves.setPreShape(shape);
}

void Voice::setRecDelayRatio(float x) {
    fadeCurves.setRecDelayRatio(x);
}

void Voice::setPreWindowRatio(float x) {
    fadeCurves.setPreWindowRatio(x);
}

void Voice::publishFlags() {
    savedRecFlag.store(recFlag, std::memory_order_relaxed);
    savedRecOnceFlag.store(sch.getRecOnceActive(), std::memory_order_relaxed);
}

float Voice::getActivePosition() {
    return static_cast<float>(sch.getActivePhase() / sampleRate);
}

float Voice::getSavedPosition() {
    return static_cast<float>(rawPhase.load(std::memory_order_relaxed) / sampleRate);
}

void Voice::stop() {
    sch.stop();
}
