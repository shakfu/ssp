// Stereo resonant lowpass on inputs 1 and 2; input 3 is cutoff CV, 1 V/oct.
// @p1 cutoff 40 20000 Hz log
// @p2 Q 1 16
global float p1, p2;

adc.chan(0) => LPF l => dac.chan(0);
adc.chan(1) => LPF r => dac.chan(1);
adc.chan(2) => Gain cv => blackhole;

while (true) {
    p1 * Math.pow(2, cv.last() / 0.2) => float f;   // SSP CV is 0.2 per volt
    Math.min(Math.max(f, 20), (second / samp) * 0.45) => f;
    f => l.freq => r.freq;
    Math.max(p2, 1) => l.Q => r.Q;
    1::ms => now;
}
