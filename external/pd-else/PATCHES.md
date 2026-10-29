# Local patches

The tree is the part of ELSE at the commit in `VERSION` that `prdt` compiles: `Source/` without the objects that
need other libraries (`plaits~`, `sfont~`, `sfz~`, `circuit~`, aubio, ffmpeg, opus, vorbis, Ableton Link), and
`Abstractions/` without the help files. `patches/prdt-multi-instance.patch` applies to it
(`patch -p1 < patches/prdt-multi-instance.patch`, from this folder). `prdt` compiles every object into one `.so`
and runs one Pd instance per module (`PDINSTANCE`, `PDTHREADS`), with two modules' audio threads at once. Each
change is marked `prdt:` in the source.

`plugins/prdt/pd/else_prdt.h`, included before every source, does the rest without editing it: an `else_` prefix on
the names cyclone also defines, `else/<name>` for every class, and Pd's MIDI byte output for `sys_putmidibyte`,
which libpd lacks.

| File | Change | Why |
|-|-|-|
| `Shared/elsefile.c` | `#C` looked up at use, not cached at setup; the proxy list under a mutex; the save panel's clock freed | each Pd instance has its own symbol table, so `[messcoll -k]`'s saved data did not restore; objects are created on a worker and can be freed on the message thread |
| `Shared/random.c`, `Audio/pluck~.c` | seed counters incremented atomically | shared by modules creating objects on two threads |
| `Shared/cJSON.c`, `Control/osc.format.c`, `Control/midi.c`, `Control/pic.c`, `Control/knob.c` | scratch statics made `__thread` | two modules' threads shared them |
| `Control/osc.parse.c` | its two scratch buffers moved into the object | as above; 320 kB, too large for every thread's TLS |
| `Audio/formant~.c`, `Audio/paf~.c`, `Audio/vosim~.c` | the main inlet's scalar is a field of the object | the class has no main signal scalar, so `obj_findsignalscalar()` returned Pd's process-wide zero, which these overwrote with NaN: every unconnected signal inlet in every module then read NaN, and `[shaper~]` indexed its table with it and crashed |
| `Control/click.c` | its canvas list under a recursive mutex; a canvas's entry freed with its last `[click]` | the list is process-wide, and every abstraction instance left an entry |
| `Control/note.c` | `notesink` made at setup and freed with its class | the first `[note]` of two modules raced to make it |
| `Control/messcoll.c` | never threaded | its I/O thread calls `clock_delay` with no Pd instance on that thread (from the source; not run) |
| `Shared/buffer.c` | the sine, cosine, Gauss, parabolic and fade tables freed with a hidden class | kept until exit, they leaked on each unload of `prdt.so` |
| `Control/canvas.*.c`, `fontsize.c`, `function.c`, `keyboard.c`, `pad.c`, `pic.c`, `Audio/scope~.c` | GUI proxies freed at once | freed by a clock, they outlived a freed instance, and libpd has no GUI to message them |
| `Control/knob.c`, `note.c`, `popmenu.c`, `button.c` | GUI proxies freed | never freed |
| 32 others | leaks freed: `dust~`, `rotate.mc~` and `trighold~` had free functions the class never registered; `listspread` made itself twice | found by LeakSanitizer creating and freeing every object and abstraction |
