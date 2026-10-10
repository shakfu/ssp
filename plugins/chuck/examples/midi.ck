// 8-voice saw synth to outputs 1-2, played from the MIDI input chosen in the general panel
// (RS + LS). Silent until notes arrive. Input 1 is cutoff CV, an octave per volt.
// @p1 cutoff 500 8500 Hz log cv 1
// @p2 release 20 2020 ms log
global float p1, p2;
global int midiNotes[128];   // the plugin writes each note here: note << 8 | velocity
global int midiCount;        // and counts them
global Event midiEvent;      // and broadcasts this after each

SawOsc osc[8];
ADSR env[8];
int held[8];
LPF f => dac.chan(0);
f => dac.chan(1);
for (0 => int i; i < 8; i++) {
    osc[i] => env[i] => f;
    -1 => held[i];
}
0 => int next;
midiCount => int read;   // globals outlive a program: skip notes sent before it loaded

fun void cutoff() {   // follows the encoder and its CV between notes
    while (true) {
        Math.max(p1, 500) => f.freq;
        10::ms => now;
    }
}
spork ~ cutoff();

while (true) {
    midiEvent => now;
    while (read < midiCount) {
        midiNotes[read % 128] => int m;
        read++;
        (m >> 8) & 0x7F => int note;
        m & 0x7F => int vel;
        if (vel > 0) {
            Std.mtof(note) => osc[next].freq;
            vel / 127.0 * 0.2 => osc[next].gain;
            env[next].set(5::ms, 100::ms, 0.7, Math.max(p2, 20)::ms);
            env[next].keyOn();
            note => held[next];
            (next + 1) % 8 => next;
        } else {
            for (0 => int i; i < 8; i++)
                if (held[i] == note) { env[i].keyOff(); -1 => held[i]; }
        }
    }
}
