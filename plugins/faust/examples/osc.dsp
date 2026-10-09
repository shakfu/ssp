declare name "osc";
declare description "Band-limited saw with a V/oct pitch input on input 1.";

import("stdfaust.lib");

base = hslider("pitch [scale:log][unit:Hz]", 110, 20, 2000, 0.01);
level = hslider("level", 0.5, 0, 1, 0.01) : si.smoo;

// input 1 reads 0.2 per volt: 1 V up is an octave up
process(cv) = os.sawtooth(base * pow(2, cv * 5)) * level <: _, _;
