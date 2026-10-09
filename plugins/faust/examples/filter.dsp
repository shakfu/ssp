declare name "filter";
declare description "Resonant low-pass on inputs 1 and 2.";

import("stdfaust.lib");

cutoff = hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1) : si.smoo;
q = hslider("resonance", 1, 0.5, 20, 0.01) : si.smoo;

process = par(i, 2, fi.resonlp(cutoff, q, 1));
