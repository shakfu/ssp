# Synth_Dexed

A copy of [Synth_Dexed](https://codeberg.org/dcoredump/Synth_Dexed) `e3f524c` (2025-09-10), the Dexed engine without JUCE. `compressor.h` and the Teensy wrapper `synth_dexed.*` are left out.

`../../synth_dexed.patch` holds every change to the upstream sources; `patch -p1` in upstream's `src/` applies it.

| Change | Why |
|-|-|
| `synth.h`: circle's `constrain`, `signed_saturate_rshift` and `millis` on any host but Teensy; `millis` from `std::chrono` | Arduino and circle provide them; Linux does not |
| `dexed.*`: CMSIS (`arm_math.h`) only on Teensy and circle; `memset` for `arm_fill_f32`; the compressor behind `DISABLE_DEXED_COMPRESSOR` | the SSP has no CMSIS |
| `dexed.cpp` `keydown`: the started voice's timestamp went to `voices[i]`, the loop counter | a steal took the wrong voice, and with every voice in use `i` equals the voice count, so it wrote past the array |
| `dexed.cpp`: rate tables filled once per rate, and the constructor under one lock | the tables are static; a new instance rewrote them while another instance's audio thread read them |

Licences: GPLv3 ([LICENSE](../../LICENSE)); the msfa files are Apache 2.0 ([LICENSE-msfa.txt](../../LICENSE-msfa.txt)). Upstream ships them as `LICENSE-GPL3.txt` and `LICENSE-APACHE2.txt`.
