declare name "lfo";
declare description "Four LFOs from one phase, as CV on outputs 1-4: sine, triangle, saw, square.";

import("stdfaust.lib");

// [cv:1]: input 1 is rate CV, an octave per volt; [cv:2]: input 2 is depth CV, 0.5 V per volt
rate = hslider("rate [scale:log][unit:Hz][cv:1]", 1, 0.01, 20, 0.001);
depth = hslider("depth [unit:V][cv:2]", 2.5, 0, 5, 0.01) * 0.2 : si.smoo;   // to 0.2 per volt

ph = os.lf_sawpos(rate);   // 0 to 1
sine = sin(2 * ma.PI * ph);
tri = 1 - 4 * abs(ph - 0.5);
saw = 2 * ph - 1;
sqr = (ph < 0.5) * 2 - 1;

process = sine, tri, saw, sqr : par(i, 4, *(depth));
