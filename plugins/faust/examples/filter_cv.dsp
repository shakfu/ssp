declare name "filter_cv";
declare description "filter.dsp with cutoff CV on input 3, 1 V/oct.";

import("stdfaust.lib");

cutoff = hslider("cutoff [scale:log][unit:Hz]", 1000, 40, 16000, 1) : si.smoo;
q = hslider("resonance", 1, 0.5, 20, 0.01) : si.smoo;

// In 3 is cutoff CV, 1 V/oct; the SSP reads 0.2 per volt
fc(cv) = max(20, min(16000, cutoff * pow(2, cv / 0.2)));

process(l, r, cv) = (l : fi.resonlp(fc(cv), q, 1)), (r : fi.resonlp(fc(cv), q, 1));
