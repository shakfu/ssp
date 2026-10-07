<CsoundSynthesizer>
<CsInstruments>
; Stereo ping-pong delay on inputs 1 and 2.
; @p1 time 10 1000 ms log
; @p2 feedback 0 0.95
; @p3 mix
; @p4 tone 500 12500 Hz log
ksmps = 32
nchnls = 2
nchnls_i = 2
0dbfs = 1
massign 0, 0                ; no MIDI: by default channel n starts instr n

instr 1
  kms   chnget "p1"
  kms   port kms, 0.1
  kfb   chnget "p2"
  kmix  chnget "p3"
  ktone chnget "p4"
  aL inch 1
  aR inch 2
  afbL init 0
  afbR init 0
  adL vdelay3 aL + afbR * kfb, a(kms), 1100   ; each side feeds the other
  adR vdelay3 aR + afbL * kfb, a(kms), 1100
  afbL tone adL, ktone
  afbR tone adR, ktone
  outs aL + (afbL - aL) * kmix, aR + (afbR - aR) * kmix
endin
schedule 1, 0, -1
</CsInstruments>
</CsoundSynthesizer>
