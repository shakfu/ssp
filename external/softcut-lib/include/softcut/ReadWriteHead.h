//
// Created by ezra on 12/6/17.
//

#ifndef CUTFADEVOICE_CUTFADEVOICELOGIC_H
#define CUTFADEVOICE_CUTFADEVOICELOGIC_H

#include <cstdint>
#include "SubHead.h"
#include "Types.h"
#include "TestBuffers.h"
#include "FadeCurves.h"

namespace softcut {

    class ReadWriteHead {
    public:

        void init(FadeCurves *fc);

        // per-sample update functions
        void processSample(sample_t in, sample_t *out);
        void processSampleNoRead(sample_t in, sample_t *out);
        void processSampleNoWrite(sample_t in, sample_t *out);

        void setSampleRate(float sr);
        void setBuffer(sample_t *buf, uint32_t size);
        void setRate(rate_t x);

	// set loop (region) start point in seconds
        void setLoopStartSeconds(float x);
	// set loop (region) end point in seconds
        void setLoopEndSeconds(float x);
        void setFadeTime(float secs);
        void setLoopFlag(bool val);

        void setRecOnceFlag(bool val);
        bool getRecOnceDone();
        bool getRecOnceActive();

	// set amplitudes
        void setRec(float x);
        void setPre(float x);

	// enqueue a position change with crossfade
        void cutToPos(float seconds);

	// immediately put both subheads in stopped state
	void stop();
	// immediately start playing  from current position (no fadein)
	void run();

        void setRecOffsetSamples(int d);

        phase_t getActivePhase();
        rate_t getRate();
        // per-subhead state, for visualizing crossfades (audio thread only)
        phase_t getHeadPhase(int i) { return head[i].phase(); }
        float getHeadFade(int i) { return head[i].fade(); }
        int getActiveHead() { return active; }
    protected:
        friend class SubHead;

    private:
        // fade in to new position (given in samples)
        // assumption: phase is in range!
        void cutToPhase(phase_t newPhase);
        void enqueueCrossfade(phase_t newPhase);
        void dequeueCrossfade();
        void takeAction(Action act);

        sample_t mixFade(sample_t x, sample_t y, float a, float b); // mix two inputs with phases
        void calcFadeInc();

    private:
        SubHead head[2];

        // Default initializers mirror the state init() establishes, so the head
        // is well-defined even if a setter or processing call runs before the
        // host calls init()/setSampleRate()/setBuffer(). buf, sr, loopFlag, pre
        // and rec in particular are not touched by init() and otherwise rely on
        // the host calling every setter first.
        sample_t *buf = nullptr;      // audio buffer (allocated elsewhere)
        float sr = 48000.f;           // sample rate
        phase_t start = 0;            // start/end points
        phase_t end = 0;
        phase_t queuedCrossfade = 0;
        bool queuedCrossfadeFlag = false;
        float fadeTime = 0.1f;        // fade time in seconds
        float fadeInc = 0.f;          // linear fade increment per sample

        int active = 0;               // current active play head index (0 or 1)
        bool loopFlag = false;        // set to loop, unset for 1-shot
        float pre = 0.f;      // pre-record level
        float rec = 0.f;      // record level
        bool recOnceFlag = false; // set to record one full loop
        bool recOnceDone = false; // triggers done to tell voice to unset rec flag
        int recOnceHead = -1; // keeps track of which subhead is writing

        rate_t rate = 1.0;    // current rate
        TestBuffers testBuf;
    };
}
#endif //CUTFADEVOICE_CUTFADEVOICELOGIC_H
