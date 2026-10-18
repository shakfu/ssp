# Changelog

## 0.1.0

First version: FluidSynth 2.6.1 playing an SF2 soundfont from MIDI, in stereo. Ships FluidR3_GM (MIT), the best-sounding of the three GM fonts tried on the SSP at the same DSP cost; the release is 132 MB larger for it. A soundfont loads into a second synth on the worker thread and swaps in, so a large one does not stall audio or MIDI; each channel's program, bank and controllers carry over. SF2 only: SF3 needs libsndfile with Ogg/Vorbis, which the build leaves out.
