# createDSPFactoryFromString / FromSignals ignore opt_level

Draft for grame-cncm/faust. Not posted.

---

**Title:** `createDSPFactoryFromString` and `createDSPFactoryFromSignals` ignore `opt_level`

## Summary

In the LLVM backend, `createDSPFactoryFromString` and `createDSPFactoryFromSignals` call `setOptlevel(opt_level)` after `initJIT()`. `initJIT()` has already optimised the module with the factory's default level. The requested `opt_level` is stored but never used, so every factory is optimised at `LLVM_MAX_OPT_LEVEL`.

fc1031d214 ("Set the JIT target before initJIT(), not after") fixed the same ordering for `setTarget`. `setOptlevel` was left after `initJIT`.

## Where

At 2.88.0 (8c00913e44); master-dev 5038da1c09 is the same:

- `llvm_code_container.cpp:289` builds the factory with `opt_level` -1, so `fOptLevel` starts at `LLVM_MAX_OPT_LEVEL`.
- `llvm_dynamic_dsp_aux.cpp:353`: `initJIT()` runs the IR pipeline at `opt_table[fOptLevel]`.
- `llvm_dynamic_dsp_aux.cpp:748` and `:788`: `setOptlevel(opt_level)` runs after `initJIT()` returns.

## Measurement

`chorus.dsp` below, compiled with `createDSPFactoryFromFile` (which calls `createDSPFactoryFromString`) at `opt_level` 0 and -1. Each factory gets its own `-cn`, so the factory cache cannot return the other level's factory. Load is `compute()` time over the audio's duration: 48 kHz, 128-frame blocks, 20 s of noise, 3 runs. Host x86-64 (Zen 3), LLVM 18.1.3, Faust 2.85.9.

| libfaust | `opt_level` 0 | `opt_level` -1 |
|-|-|-|
| as released | 0.069-0.073% | 0.068% |
| `setOptlevel` moved before `initJIT` | 0.131-0.132% | 0.067-0.068% |

As released, level 0 performs like the maximum. With the order fixed, level 0 is about 2x slower, as expected. I measured 2.85.9; the 2.88.0 lines above have the same order.

```faust
import("stdfaust.lib");

rate  = hslider("rate",  0.30, 0, 1, 0.001) : si.smoo;
depth = hslider("depth", 0.50, 0, 1, 0.001) : si.smoo;
del   = hslider("delay", 0.40, 0, 1, 0.001) : si.smoo;
mix   = hslider("mix",   0.50, 0, 1, 0.001) : si.smoo;

rateHz = 0.05 + rate * 5.0;
baseSamp = (0.005 + del * 0.015) * ma.SR;
modSamp  = depth * 0.005 * ma.SR;

delL = baseSamp + modSamp * (0.5 + 0.5 * os.m_oscsin(rateHz));
delR = baseSamp + modSamp * (0.5 + 0.5 * os.m_oscsin(rateHz * 1.03));

wet(d, x) = de.fdelay(2048, d, x);

chL(x) = x * (1.0 - mix) + wet(delL, x) * mix;
chR(x) = x * (1.0 - mix) + wet(delR, x) * mix;

process = chL, chR;
```

## Proposed fix

Set the level before `initJIT()`, as fc1031d214 does for the target. In both functions:

```diff
         if (factory_aux) {
             factory_aux->setTarget(target);
+            factory_aux->setOptlevel(opt_level);
         }
         if (factory_aux && factory_aux->initJIT(error_msg)) {
-            factory_aux->setOptlevel(opt_level);
             factory_aux->setClassName(getParam(argc, argv, "-cn", "mydsp"));
```

## Context

We hit the target half of this on 2.85.9, on an ARMv7 board (Rockchip RK3288, Cortex-A17) with its system LLVM 9.0.1. LLVM 9's `sys::getHostCPUName()` returns `generic` for that CPU. The JIT ignored our explicit `arm-...-gnueabihf:cortex-a17` target and generated ARMv4 code without VFP. Its calls to libm's hard-float `tanf` passed and read floats in core registers, so `tan()` returned garbage. fc1031d214 fixes that case; thank you. We found the `opt_level` ordering while patching 2.85.9.
