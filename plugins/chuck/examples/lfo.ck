// Four LFOs at one rate, as CV on outputs 1-4: sine, triangle, saw, square, +-depth volts.
// Input 1 is rate CV, an octave per volt; input 2 is depth CV, 0.5 V per volt.
// @p1 rate 0.01 20 Hz log cv 1
// @p2 depth 0 5 V cv 2
global float p1, p2;

SinOsc sine => dac.chan(0);
TriOsc tri => dac.chan(1);
SawOsc saw => dac.chan(2);
SqrOsc sqr => dac.chan(3);
[sine, tri, saw, sqr] @=> Osc lfos[];

while (true) {
    for (0 => int i; i < lfos.size(); i++) {
        p1 => lfos[i].freq;
        p2 * 0.2 => lfos[i].gain;   // volts to the SSP's 0.2 per volt
    }
    10::ms => now;                  // chuk updates p1 and p2 every 10 ms
}
