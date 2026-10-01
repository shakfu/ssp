//
// Created by ezra on 11/16/18.
//
// NB: this is an empty stub. Upstream embeds `float buf[numChannels][131072]`
// (~3 MB) plus <fstream> in every ReadWriteHead, purely to dump a Matlab trace
// for desktop debugging. Nothing in the library calls update()/print() (only
// init()), so for the host/Python build it is replaced with a no-op stub to
// avoid ~3 MB per voice and pulling in iostream/fstream. Restore the original
// if you need the Matlab dump.

#ifndef Softcut_TESTBUFFERS_H
#define Softcut_TESTBUFFERS_H

namespace softcut {
class TestBuffers {
public:
    typedef enum { Read, Write, Fade, State, Pre, Rec, numChannels } Channel;

    void init() {}

    void update(float readPhase, float writePhase, float fade, float state, float pre, float rec) {
        (void)readPhase; (void)writePhase; (void)fade; (void)state; (void)pre; (void)rec;
    }

    void print() {}
};
}

#endif //Softcut_TESTBUFFERS_H
