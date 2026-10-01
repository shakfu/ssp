# softcut-lib (vendored, patched)

DSP core of the `sfct` module. Upstream is [monome/softcut-lib](https://github.com/monome/softcut-lib)
at commit `14241f2a94a7cd2e6c91b9bdc4e500d68abef907` (2024-12-31), GPL-3.0.

`include/` and `src/` are copied from [softcut-py](https://github.com/shakfu/softcut-py)
`thirdparty/softcut-lib` (softcut-py commit `6e714ae`), with both files in `patches/` already applied:

- `softcut-lib-fixes.patch`: initialises `Svf` and `Voice` state that upstream leaves to zeroed
  static storage. A host-allocated `Voice` otherwise yields NaN.
- `softcut-lib.patch`: adds `Voice(bool fixQuirks)`, fade-shape setters and thread-safe head readback.

The patched tree is vendored rather than kept as a submodule because the patches are not upstream.
To update, re-copy from softcut-py; do not edit these files here.
