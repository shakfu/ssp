<CsoundSynthesizer>
<CsInstruments>
; Stereo ladder filter on inputs 1 and 2; input 3 is cutoff CV, 1 V/oct.
; @p1 cutoff 40 20000 Hz log
; @p2 resonance
; @p3 drive 1 9
; @p4 mix
ksmps = 32
nchnls = 2
nchnls_i = 3
0dbfs = 1
massign 0, 0                ; no MIDI: by default channel n starts instr n

instr 1
  kcv   = k(inch(3)) / 0.2              ; SSP CV is 0.2 per volt
  kfreq limit chnget:k("p1") * 2 ^ kcv, 20, sr * 0.45
  kfreq port kfreq, 0.02
  kres  chnget "p2"
  kgain chnget "p3"
  kmix  chnget "p4"
  aL inch 1
  aR inch 2
  afL moogladder tanh(aL * kgain), kfreq, kres
  afR moogladder tanh(aR * kgain), kfreq, kres
  outs aL + (afL - aL) * kmix, aR + (afR - aR) * kmix
endin
schedule 1, 0, -1
</CsInstruments>
</CsoundSynthesizer>
