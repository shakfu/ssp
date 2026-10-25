# monome grids and arcs on the SSP

Status, 2026-10-25: libmonome drives a grid on the SSP (keys, LED levels, open and close). `gseq` uses it: `plugins/common/monome` (`Grid.h`, the frame and its quadrant diff; `GridDevice`, the device and its thread) and `plugins/gseq/Source/GridView`; layout in `plugins/gseq/README.md` (Monome grid), device steps 47-56 in [gseq-device-test.md](gseq-device-test.md), not yet run. Next: `ttyp`'s grid ops.

## The driver problem

monome grids and arcs are FTDI USB-serial devices (`0403:6001`). The SSP's kernel (5.13.13, Percussa's build) has no `usbserial`, `ftdi_sio` or `cdc_acm`, built in or as modules, so a grid gets no `/dev/ttyUSB*`. Its `/lib/modules/<version>/build` link points to the Percussa developer's machine, so matching modules cannot be built either. usbfs is there (`/dev/bus/usb`, root read-write; Synthor runs as root), and no driver claims the grid's interface.

So libmonome reaches the chip from userspace: the fork [shakfu/libmonome](https://github.com/shakfu/libmonome) adds `src/platform/libftdi.c` (CMake option `MONOME_LIBFTDI`), over libftdi1 1.5 and libusb-1.0 1.0.27, built without udev by `scripts/build_monome.sh`. Building the kernel modules was the other option; it needs the exact kernel tree and loads self-built code into the SSP's kernel.

## Using it

- A device is named `ftdi:<serial>`, such as `ftdi:m1000825`; the serial's prefix picks the protocol (`m`: mext, varibright). Finding the serials of attached grids is the caller's: `ftdi_usb_find_all` lists them.
- There is no file descriptor: `monome_get_fd()` returns -1. `monome_poll_group_wait`, `monome_event_loop` and the protocol's reads poll the chip instead, which answers every latency-timer period, 1 ms.
- Idle, a process polling one grid on the SSP used, by latency timer: 1 ms, 5% of a core; 2 ms, 3%; 4 ms, 2%; 8 ms, 1%. The fork uses 4 ms: keys arrive up to 3 ms later than at 1 ms.
- Link `libmonome.a`, `libftdi1.a`, `libusb-1.0.a` and pthread from `build/deps/<target>/lib`.

## Device check

Done 2026-10-25 with a 16 x 8 grid (`m1000825`) on the SSP's hub, beside a running Synthor, with a probe linked against the pinned fork: opened as mext, 16 x 8, "monome 128"; a ramp of the 16 LED levels across the columns; 34 presses on 21 keys, all four corners, chords of two and taps 100 ms apart, every down paired with an up; clean close. `scripts/test_build_monome.py` covers the failure paths on the host under ASan.

Not yet checked: the arc; libmonome's `examples/test.c` LED patterns; unplugging a grid while open.

## Fork fix

The fork's `031a87f` freed the serial on `monome_open`'s success path, which the protocol keeps as `monome->serial`: every device got a dangling serial, and `monome_close` freed it twice. Fixed in `7c18fdf`.
