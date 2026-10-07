<CsoundSynthesizer>
<CsInstruments>
; Polyphonic two-operator FM synth, played over MIDI on any channel.
; Ratio, attack and release apply from the next note.
; @p1 ratio 0.5 7.5
; @p2 index 0 10
; @p3 attack 0.002 1 s log
; @p4 release 0.02 3 s log
ksmps = 32
nchnls = 2
0dbfs = 1
massign 0, "Voice"

instr Voice
  ifreq  cpsmidi
  iamp   ampmidi 0.3
  iratio chnget "p1"
  kindex chnget "p2"
  iatt   chnget "p3"
  irel   chnget "p4"
  aenv madsr iatt, 0.2, 0.7, irel
  amod oscili ifreq * iratio * kindex, ifreq * iratio
  acar oscili iamp, ifreq + amod
  outs acar * aenv, acar * aenv
endin
</CsInstruments>
</CsoundSynthesizer>
