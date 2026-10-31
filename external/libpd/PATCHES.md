# Local patches

The tree is libpd and Pure Data at the commits in `VERSION`, with these changes. Reapply them after an update
(`patch -p1 < patches/<name>.patch`, from this folder).

| Patch | Change | Why |
|-|-|-|
| `pd-freeprocess.patch` | adds `pd_freeprocess()` to `m_class.c`, declared in `m_imp.h` | Pd has no teardown (`pd_term_systems` is a TODO). `prdt` calls it at `dlclose`, after its last instance, so unloading `prdt.so` frees Pd's classes, main instance, and `cos~` and FFT tables: about 210 kB per unload on the SSP before, flat after |
| `pd-abstraction-unbind.patch` | `canvas_free()` clears `#A` if it is the canvas being freed | `canvas_popabstraction()` binds each new abstraction to `#A` and nothing unbinds it. One made after its patch loaded (a `[clone]` resized, dynamic patching) left `#A` pointing at freed memory, which `garray_free()` and `[text define]`'s free then read: a use-after-free when `prdt` frees the patch. ELSE's `[echo.rev~]` found it; `prdt_test`'s `testCloneArray` repeats it in vanilla |
| `pd-atomic-glist-valid.patch` | `glist_valid`, which validates `[pointer]`s, incremented atomically through `glist_newvalid()` | it is process-wide, and modules resize arrays on their own threads: a lost increment could take it backwards, give a container a number it had before, and let a stale pointer to it pass as valid. TSan found it with two `[sfload]`s resizing arrays at once; vanilla `[soundfiler read -resize]` does the same |
