declare name "poly";
declare description "8-voice saw synth, played over MIDI. Input 1 is cutoff CV, an octave per volt.";
declare options "[nvoices:8]";

import("stdfaust.lib");

// MIDI notes set freq, gain and gate in a free voice; they stay off the encoders
freq = hslider("freq", 440, 20, 20000, 1);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

cutoff = hslider("cutoff [scale:log][unit:Hz][cv:1]", 2000, 100, 12000, 1) : si.smoo;
res = hslider("res", 0.3, 0, 0.95, 0.01);
attack = hslider("attack [unit:ms]", 5, 1, 1000, 1) / 1000;
release = hslider("release [unit:ms]", 300, 10, 4000, 1) / 1000;

env = en.adsr(attack, 0.1, 0.8, release, gate);
voice = os.sawtooth(freq) : fi.resonlp(cutoff, 1 / (1 - res), 1) : *(env * gain * 0.15);
process = voice <: _, _;
