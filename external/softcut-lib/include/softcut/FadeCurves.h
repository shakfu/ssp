//
// Created by ezra on 11/15/18.
//
// static class for producing curves in fade period
//
// FIXME: this should be an object owned by SoftcutHead, passed to child SubHeads


#ifndef Softcut_FADECURVES_H
#define Softcut_FADECURVES_H

namespace softcut {

    class FadeCurves {
    public:
        typedef enum { Linear=0, Sine=1, Raised=2 } Shape;

        // initialize with defaults. fixQuirks corrects the raised rec curve's
        // sign (upstream records polarity-inverted) and the pre-shape check.
        void init(bool fixQuirks = false);
         void setRecDelayRatio(float x);
         void setPreWindowRatio(float x);
         void setMinRecDelayFrames(unsigned int x);
         void setMinPreWindowFrames(unsigned int x);
        // set curve shape
         void setPreShape(Shape x);
         void setRecShape(Shape x);
        // x is assumed to be in [0,1]
         float getRecFadeValue(float x);

         float getPreFadeValue(float x);

        // xfade curve buffers
        static constexpr unsigned int fadeBufSize = 1001;

    private:
         void calcPreFade();
         void calcRecFade();

    private:

        // record delay and pre window in fade, as proportion of fade time.
        // NB: these are default-initialized to the same values init() assigns,
        // because init() calls setPreShape()/setRecShape() (which run
        // calcPreFade()/calcRecFade()) *before* assigning these members. Without
        // the initializers the first calc reads garbage and a huge window ratio
        // overruns the fadeBufSize stack buffer in calc*Fade().
         float recDelayRatio = 1.f / (8 * 16);
         float preWindowRatio = 1.f / 8;
        // minimum record delay/pre window, in frames
         unsigned int recDelayMinFrames = 0;
         unsigned int preWindowMinFrames = 0;
         float recFadeBuf[fadeBufSize];
         float preFadeBuf[fadeBufSize];
         Shape recShape = Raised;
         Shape preShape = Linear;
         bool fixQuirks = false;
    };
}

#endif //Softcut_FADECURVES_H
