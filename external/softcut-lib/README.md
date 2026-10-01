# softcut-lib (vendored, patched)

DSP core of the `sfct` module. Upstream is [monome/softcut-lib](https://github.com/monome/softcut-lib)
at commit `14241f2a94a7cd2e6c91b9bdc4e500d68abef907` (2024-12-31), GPL-3.0.

`include/` and `src/` are copied from [softcut-py](https://github.com/shakfu/softcut-py)
`thirdparty/softcut-lib` (softcut-py commit `6e714ae`), with its two patches, copied to `patches/`, already applied:

- `softcut-lib-fixes.patch`: initialises `Svf` and `Voice` state that upstream leaves to zeroed
  static storage. A host-allocated `Voice` otherwise yields NaN.
- `softcut-lib.patch`: adds `Voice(bool fixQuirks)`, fade-shape setters and thread-safe head readback.

sfct adds a third, `sfct-perf.patch`, which removes per-sample work: `setRate` while the rate ramp
holds still, `sinf` at fade 0 and 1, and reading a silent head. Output is byte-identical; it cuts
the time per block by 21% on x86. It belongs in softcut-py too.

The patched tree is vendored rather than kept as a submodule because the patches are not upstream.
To update, re-copy from softcut-py and apply `sfct-perf.patch` if softcut-py lacks it.
