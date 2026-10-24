# fsyn: device test

`plugins/fsyn/tests/run_on_ssp.sh` passed on 2026-10-18, beside Synthor: the engine tests with TimGM6mb, and the benchmark of TimGM6mb, GeneralUser-GS and FluidR3_GM in [plugins/fsyn/README.md](../../plugins/fsyn/README.md#soundfonts). In the benchmark, load time is from the card with a cold cache; dsp% is one core's share for 8 channels of held chords plus drums, reverb and chorus on. SF3 fonts do not load because the build leaves out libsndfile, which decodes them. The steps below test the module in Synthor.

2026-10-18, user, in Synthor with `alda`: FluidR3_GM plays at 4-5% DSP average, 9-10% peak, and sounds best; GeneralUser-GS second, at the same cost; TimGM6mb costs the same and sounds worst. Steps 2-3 pass on that report; the others are not yet run.

## Setup

- Install, with Synthor stopped (`kill -9 $(pidof SYNTHOR)`, then power-cycle after): `make deploy-mod MOD=fsyn`.

- Soundfonts in `/media/BOOT/fsyn`: `TimGM6mb.sf2`, `GeneralUser-GS.sf2` and `FluidR3_GM.sf2` (copied 2026-10-18).

- `alda`, with the examples in `BOOT/alda`.

| # | Do | Expect |
|-|-|-|
| 1 | Add `fsyn` to a patch; show it | listed as `fsyn`; input `Gain CV`, outputs `Out L`, `Out R`; status `FluidR3_GM.sf2`, `voices 0` |
| 2 | Add `alda`; set fsyn's MIDI IN to `1:alda`; patch fsyn's outs to the audio outs | `alda`'s default score plays as piano and bass |
| 3 | In alda, load `orchestra.alda` | each part plays its GM instrument; DSP % in the status |
| 4 | Load `percussion.alda` | the GM drum kit on channel 10 |
| 5 | Load (button 5) `GeneralUser-GS.sf2` while a score plays | `loading GeneralUser-GS.sf2`; audio continues, then the new font plays the same instruments; notes held at the swap end |
| 6 | Load `FluidR3_GM.sf2` again while a score plays | as step 5; about 2.4 s of `loading`; no audio dropout |
| 7 | Load a non-SF2 file, such as an `.alda` | FluidSynth's reason in orange; the font playing stays |
| 8 | Turn Reverb and Chorus from 0 to 1 | the effect from none to full |
| 9 | Voices to 8 with `orchestra.alda` | notes drop out; voices at most 8 |
| 10 | Gain CV: 1 V into in 1 | louder, as Gain +0.2 |
| 11 | Save the patch, power-cycle, load it | the same font and controls |
| 12 | Two `fsyn`, the same font | both play; RAM for the samples once (FluidSynth's sample cache) |
