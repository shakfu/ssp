declare name "osc";
declare description "Band-limited saw. Input 1 is pitch CV, 1 V/oct; input 2 is level CV.";

import("stdfaust.lib");

// [cv:N]: the module adds input N to the control, so the screen shows the moved values
base = hslider("pitch [scale:log][unit:Hz][cv:1]", 110, 20, 2000, 0.01);
level = hslider("level [cv:2]", 0.5, 0, 1, 0.01) : si.smoo;

process = os.sawtooth(base) * level <: _, _;
