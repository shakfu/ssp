// 8-voice saw synth on ChucK's MIDI input device 0, any channel. Silent without a MIDI device.
// @p1 cutoff 500 8500 Hz log
// @p2 release 20 2020 ms log
global float p1, p2;

MidiIn min;
MidiMsg msg;
if (!min.open(0)) me.exit();

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

while (true) {
    min => now;
    while (min.recv(msg)) {
        msg.data1 & 0xF0 => int status;
        msg.data2 => int note;
        msg.data3 => int vel;
        Math.max(p1, 500) => f.freq;
        if (status == 0x90 && vel > 0) {
            Std.mtof(note) => osc[next].freq;
            vel / 127.0 * 0.2 => osc[next].gain;
            env[next].set(5::ms, 100::ms, 0.7, Math.max(p2, 20)::ms);
            env[next].keyOn();
            note => held[next];
            (next + 1) % 8 => next;
        } else if (status == 0x80 || status == 0x90) {
            for (0 => int i; i < 8; i++)
                if (held[i] == note) { env[i].keyOff(); -1 => held[i]; }
        }
    }
}
