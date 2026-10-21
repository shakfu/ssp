# cpsoct of a negative octave; pluck with a negative frequency

Draft for csound/csound. Not posted.

---

**Title:** cpsoct returns garbage for negative octaves; pluck then reads out of bounds and crashes

## Summary

`cpsoct` of a negative argument returns a large, often negative number: `cpsoct(-0.01)` is
-4.36e9. `pluck` given that frequency reads before its buffer and segfaults. Both bugs are in
7.0.0 and 6.18, and in `develop` as of 2026-10-21.

## Reproduce

```csound
<CsoundSynthesizer>
<CsOptions>
-n -d
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 64
nchnls = 1
0dbfs = 1
instr 1
  a1 pluck 0.5, cpsoct(-0.01), 440, 0, 1
  out a1
endin
</CsInstruments>
<CsScore>
i 1 0 1
</CsScore>
</CsoundSynthesizer>
```

`csound repro.csd` ends with `csound command: Segmentation fault`.

| Build | Result |
|-|-|
| 7.0.0, float samples, built from the tag, Linux x86_64 | segfault |
| 6.18, double samples, Ubuntu package | segfault |

## Cause 1: `CPSOCTL` shifts by a negative count

```c
// include/coreDefs.h:87
#define CPSOCTL(n) ((cs_float)(1<<((int32_t)(n)>>13))*csound->cpsocfrc[(int32_t)(n)&8191])
```

For `n < 0`, `n >> 13` is negative, and `1 << negative` is undefined behaviour in C. `cpsoct`
(`OOps/aops.c:1190`) passes `(int32_t)(x * OCTRES)` straight in:

| x | `cpsoct(x)`, 7.0.0 float | `cpsoct(x)`, 6.18 double | expected |
|-|-|-|-|
| 0.01 | 1.029003 | 1.029003 | 1.029003 |
| -0.01 | -4359368192 | -4359368510.6 | 1.0148 |
| -1 | -2194674432 | -2194674310.1 | 0.5109 |
| -5 | 137167152 | 137167144.4 | 0.0319 |

The same macro serves `acpsoct` and the MIDI converters in `OOps/midiops.c` and
`OOps/midiinterop.c`.

A possible fix: scale by a power of two with `ldexp`, which takes a negative exponent. An
arithmetic right shift rounds down, so `n & 8191` stays the right table index for negative `n`
in two's complement:

```c
#define CPSOCTL(n) ((cs_float)ldexp(csound->cpsocfrc[(int32_t)(n)&8191], (int32_t)(n)>>13))
```

Right-shifting a negative signed integer is implementation-defined in C, though arithmetic on
every compiler Csound targets. A version that avoids it divides with `floor` first.

## Cause 2: `pluck` checks only the upper bound of its phase increment

```c
// OOps/ugens4.c:419-422
phsinc = (int32_t)(*p->kcps * p->sicps);
...
if (UNLIKELY(phsinc > ltwopi)) goto err2;
```

A negative `kcps` gives a negative `phsinc`. The read index `phs256 >> 8` then goes negative,
and `fp[0]` and `fp[1]` read before `auxch.auxp`. At -440 Hz the read stays near the buffer and
gives garbage samples (2597 out of range in a 1 s note). At -4.36e9 Hz the read segfaults.
The `int32_t` conversion of a product outside its range is also undefined.

A possible fix: check the product in floating point before converting it.

```c
cs_float inc = *p->kcps * p->sicps;
if (UNLIKELY(!(inc >= FL(0.0) && inc <= (cs_float)ltwopi))) goto err2;
phsinc = (int32_t)inc;
```

`!(a && b)` also rejects NaN.

## Where it showed up

`examples/xanadu.csd` in this repository crashes when MIDI starts instrument 1. A MIDI note
leaves `p5` at 0, so `ioct = octpch(0) = 0`. The vibrato `kvib` swings +-1/120, so
`cpsoct(ioct+kvib)` goes negative half the time, and `pluck` crashes. We found it embedding
Csound 7.0.0 in an audio plugin host that passes MIDI to every orchestra; the crash took down
the host process.
