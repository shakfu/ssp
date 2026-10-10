<CsoundSynthesizer>
<CsInstruments>
; Four LFOs from one phase, as CV on outputs 1 to 4: sine, triangle, saw, square, +-depth volts.
; Input 1 is rate CV, an octave per volt; input 2 is depth CV, 0.5 V per volt.
; @p1 rate 0.01 20 Hz log cv 1
; @p2 depth 0 5 V cv 2
ksmps = 32
nchnls = 4
0dbfs = 1
massign 0, 0                ; no MIDI: by default channel n starts instr n

instr 1
  krate chnget "p1"
  kamp  = chnget:k("p2") * 0.2    ; volts to the SSP's 0.2 per volt
  aph   phasor krate              ; 0 to 1
  asin  = sin(aph * 2 * $M_PI)
  atri  = 1 - 4 * abs(aph - 0.5)  ; -1 at 0, 1 at 0.5
  asaw  = 2 * aph - 1
  asqr  = 1 - 2 * floor(aph * 2)  ; 1 for the first half, -1 for the second
  outch 1, asin * kamp, 2, atri * kamp, 3, asaw * kamp, 4, asqr * kamp
endin
schedule 1, 0, -1
</CsInstruments>
</CsoundSynthesizer>
