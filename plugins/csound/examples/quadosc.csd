<CsoundSynthesizer>
<CsInstruments>
; Four waveforms of one oscillator on outputs 1 to 4: sine, triangle, saw, pulse.
; Input 1 is pitch CV, 1 V/oct; input 2 is pulse width CV. The module adds the CV to p1 and p2
; (the `cv` at the end of their lines), so the screen shows the moved values.
; @p1 pitch 32.7 2093 Hz log cv 1
; @p2 width 0.05 0.95 cv 2
ksmps = 32
nchnls = 4
0dbfs = 1
massign 0, 0                ; no MIDI: by default channel n starts instr n

instr 1
  kfreq chnget "p1"
  kpw   chnget "p2"
  a1 oscili 0.5, kfreq
  a2 vco2 0.5, kfreq, 12          ; triangle
  a3 vco2 0.5, kfreq, 0           ; saw
  a4 vco2 0.5, kfreq, 2, kpw      ; pulse
  outch 1, a1, 2, a2, 3, a3, 4, a4
endin
schedule 1, 0, -1
</CsInstruments>
</CsoundSynthesizer>
