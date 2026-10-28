# Local patches

The tree is cyclone at the tag in `VERSION`, with `patches/prdt-multi-instance.patch` applied
(`patch -p1 < patches/prdt-multi-instance.patch`, from this folder). `prdt` compiles every object into one
`.so` and runs one Pd instance per module (`PDINSTANCE`, `PDTHREADS`), with two modules' audio threads at once.
Upstream builds each object as its own external for one Pd instance. Each change is marked `prdt:` in the
source.

| File | Change | Why |
|-|-|-|
| `onepole.c`, `svf.c`, `zl.c`, `shared/common/file.c` | symbols looked up at use, not cached at setup | each Pd instance has its own symbol table, so a symbol cached by the main instance never equals the module's: `[onepole~ 0.5 linear]` ran in Hz mode, `zl`'s group, stream, queue and scramble checks were always false, and a `[coll @embed 1]`'s saved data did not restore (`table` and `funbuff` embed through the same code) |
| `tosymbol.c`, `fromsymbol.c`, `pv.c`, `seq.c`, `shared/control/rand.c` | per-call scratch statics made `__thread` | two modules' threads shared them |
| `shared/common/random.c` | id counter incremented atomically | shared by modules creating objects on two threads; per-thread counters would give two modules the same random streams |
| `shared/common/file.c` | the proxy list under a mutex | objects are created on a worker and can be freed on the message thread |
| `cycle.c` | cosine table built at setup and freed with the class | built lazily, two first `[cycle~]`s raced; never freed, it leaked on each unload of `prdt.so` |
| `coll.c` | never threaded | the I/O thread calls `clock_delay` with no Pd instance on that thread (`pd_this` is per thread), so the first threaded read would crash (from the source; not run) |
| `mtr.c` | frees its and its tracks' file handles | a freed `[mtr]` stayed on `file.c`'s list and bound to `#C`: a use-after-free in the next file object's free |
| `cyclone_lib.c`, `zl.c`, `spray.c`, `lookup.c`, `gate.c`, `record.c`, `scope.c`, `comment.c`, `shared/common/file.c` | leaks freed | found by LeakSanitizer creating and freeing every object; `record~`, `scope~` and `comment` free their proxy at once, as libpd has no GUI to message it |

`plugins/prdt/pd/CMakeLists.txt` also replaces `shared/control/s_cycloneutf8.c`, a copy of Pd's `s_utf8.c` whose
functions would link twice, with `plugins/prdt/pd/cyclone_utf8.c`, and leaves out `loadmess` (it defines Pd's
`sys_noloadbang`), `scope_dialog`, and `mousestate`, `mousefilter` and `active`, which read the mouse.
