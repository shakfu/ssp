// 8-step random sequencer. Outputs 1-2 audio; 3 gate; 4 pitch CV, 1 V/oct from 0 V.
// Turning reroll past half rolls a new pattern. Input 1 is tempo CV, 20 bpm per volt; input 2 is
// decay CV, double per volt. The module adds the CV to p1 and p3 (the `cv` at the end of their
// lines), so the screen shows the moved values.
// @p1 tempo 40 240 bpm cv 1
// @p2 octaves 0 2
// @p3 decay 20 620 ms log cv 2
// @p4 reroll
global float p1, p2, p3, p4;

SinOsc s => ADSR e => Gain g => dac.chan(0);
g => dac.chan(1);
0.3 => g.gain;
Step gate => dac.chan(2);
Step pitch => dac.chan(3);

[0, 2, 3, 5, 7, 8, 10, 12] @=> int scale[];
int notes[8];

fun void roll() {
    for (0 => int i; i < 8; i++)
        scale[Math.random2(0, 7)] + 12 * Math.random2(0, Math.round(p2) $ int) => notes[i];
}

roll();
0 => int step;
0 => int rolled;
while (true) {
    if (p4 > 0.5 && !rolled) { roll(); 1 => rolled; }
    else if (p4 <= 0.5) 0 => rolled;

    notes[step] => int n;
    Std.mtof(48 + n) => s.freq;
    n / 12.0 * 0.2 => pitch.next;             // 0.2 per volt
    e.set(2::ms, Math.max(p3, 20)::ms, 0, 10::ms);
    (60.0 / Math.max(p1, 40) / 4)::second => dur len;   // sixteenths

    1 => gate.next;
    e.keyOn();
    len / 2 => now;
    0 => gate.next;
    e.keyOff();
    len / 2 => now;
    (step + 1) % 8 => step;
}
