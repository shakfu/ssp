declare name "tremolo";
declare description "Stereo tremolo: a sine LFO on the level of inputs 1 and 2.";

import("stdfaust.lib");

rate = hslider("[0] rate [unit:Hz]", 4, 0.1, 20, 0.01);
depth = hslider("[1] depth", 0.5, 0, 1, 0.01) : si.smoo;

gain = 1 - depth * (0.5 + 0.5 * os.osc(rate));

process = par(i, 2, *(gain));
