# Local patches

The tree is libpd and Pure Data at the commits in `VERSION`, with these changes. Reapply them after an update
(`patch -p1 < patches/<name>.patch`, from this folder).

| Patch | Change | Why |
|-|-|-|
| `pd-freeprocess.patch` | adds `pd_freeprocess()` to `m_class.c`, declared in `m_imp.h` | Pd has no teardown (`pd_term_systems` is a TODO). `prdt` calls it at `dlclose`, after its last instance, so unloading `prdt.so` frees Pd's classes, main instance, and `cos~` and FFT tables: about 210 kB per unload on the SSP before, flat after |
