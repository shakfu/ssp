//
// Created by ezra on 11/3/18.
//

#ifndef Softcut_SoftcutVOICE_H
#define Softcut_SoftcutVOICE_H

#include <array>
#include <atomic>

#include "ReadWriteHead.h"
#include "Svf.h"
#include "Utilities.h"
#include "FadeCurves.h"

namespace softcut {
    class Voice {
    public:
        // fixQuirks corrects upstream defects: polarity-inverted recording,
        // the 0.1 s fade time reset() leaves, and the raised pre-curve check.
        explicit Voice(bool fixQuirks = false);

        void init(FadeCurves *fc);

        void setBuffer(float *buf, unsigned int numFrames);

        void setSampleRate(float hz);

        void setRate(float rate);

        void setLoopStart(float sec);

        void setLoopEnd(float sec);

        void setLoopFlag(bool val);

        void setFadeTime(float sec);

        void setRecLevel(float amp);

        void setPreLevel(float amp);

        void setRecFlag(bool val);

        void setRecOnceFlag(bool val);

        void setPlayFlag(bool val);

        void setPreFilterFc(float);

        void setPreFilterRq(float);

        void setPreFilterLp(float);

        void setPreFilterHp(float);

        void setPreFilterBp(float);

        void setPreFilterBr(float);

        void setPreFilterDry(float);

        void setPreFilterFcMod(float x);

        void setPostFilterFc(float);

        void setPostFilterRq(float);

        void setPostFilterLp(float);

        void setPostFilterHp(float);

        void setPostFilterBp(float);

        void setPostFilterBr(float);

        void setPostFilterDry(float);

        // crossfade curves; see FadeCurves
        void setRecFadeShape(FadeCurves::Shape shape);

        void setPreFadeShape(FadeCurves::Shape shape);

        void setRecDelayRatio(float x);

        void setPreWindowRatio(float x);

        void cutToPos(float sec);

        // process a single channel
        void processBlockMono(const float *in, float *out, int numFrames);

        void setRecOffset(float d);

        void setRecPreSlewTime(float d);

        void setRateSlewTime(float d);

        void setPhaseQuant(float x);

        void setPhaseOffset(float x);

        phase_t getQuantPhase();

        bool getPlayFlag();

        bool getRecFlag();

        // rec and rec-once state, updated per block and on each flag change;
        // use these from non-audio threads
        bool getSavedRecFlag();

        bool getSavedRecOnceFlag();

        // subhead i's position (seconds) and fade, and which subhead is
        // active, as of the last block; use these from non-audio threads
        float getSavedHeadPosition(int i);

        float getSavedHeadFade(int i);

        int getSavedActiveHead();

	float getActivePosition();

	// use this from non-audio threads
        float getSavedPosition();

        void reset();

	// immediately put both subheads in a stopped state
	void stop();

    private:
        void updatePreSvfFc();

        void updateQuantPhase();

        void publishFlags();

        void publishHeads();

    private:
        float *buf;
        int bufFrames;
        float sampleRate;

        // fade curve data
        FadeCurves fadeCurves;
        // xfaded read/write head
        ReadWriteHead sch;
        // input filter
        Svf svfPre;
        // output filter
        Svf svfPost;
        // rate ramp
        LogRamp rateRamp;
        // pre-level ramp
        LogRamp preRamp;
        // record-level ramp
        LogRamp recRamp;


        // default frequency for SVF
        // reduced automatically when setting rate
        float svfPreFcBase;
        // the amount by which SVF frequency is modulated by rate
        float svfPreFcMod = 1.0;
        float svfPreDryLevel = 1.0;
        float svfPostDryLevel = 1.0;
        // NB: like SubHead's, these members are default-initialized because
        // softcut relies on zero-initialized static storage on embedded
        // targets. reset() did not set them either, so on a host a
        // heap-allocated Voice began with a garbage phaseQuant -- which
        // updateQuantPhase() divides by, taking the quantizing branch instead
        // of the phaseQuant == 0 one -- and with garbage in the two phase
        // mirrors, which non-audio threads read before the first block is
        // processed. Both reach a host as a nonsense quantized phase: the OSC
        // phase poll reports it, and a value beyond float range aborts the
        // send outright.
        //
        // phase quantization unit, should be in [0,1]
        phase_t phaseQuant = 0;
        // phase offset in sec
        float phaseOffset = 0;

	//-- these stored phases are for access from non-audio threads,
	// and are updated once per block:
	std::atomic<phase_t> rawPhase{0};
        std::atomic<phase_t> quantPhase{0};
        std::atomic<bool> savedRecFlag{false};
        std::atomic<bool> savedRecOnceFlag{false};
        std::atomic<phase_t> savedHeadPhase[2] = {{0}, {0}};
        std::atomic<float> savedHeadFade[2] = {{0.f}, {0.f}};
        std::atomic<int> savedActiveHead{0};

    private:

        bool playFlag;
        bool recFlag;
        bool fixQuirks = false;

    };
}


#endif //Softcut_SoftcutVOICE_H
